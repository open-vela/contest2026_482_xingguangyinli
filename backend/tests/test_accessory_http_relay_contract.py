import importlib.util
import io
import json
import urllib.error
from pathlib import Path

from fastapi.testclient import TestClient

from living_canvas_backend.app import create_app


TOKEN = "synthetic-accessory-token-at-least-32-characters"
REPO = Path(__file__).resolve().parents[2]
RELAY_PATH = REPO / "app" / "hello_app" / "tools" / "accessory_http_relay.py"


class RelayResponse:
    def __init__(self, response):
        self.status = response.status_code
        self._body = response.content

    def __enter__(self):
        return self

    def __exit__(self, *_args):
        return False

    def read(self, limit):
        return self._body[:limit]


def load_relay_module():
    spec = importlib.util.spec_from_file_location("accessory_http_relay", RELAY_PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_mmwave_http_relay_matches_real_backend_contract(tmp_path):
    client = TestClient(create_app(
        data_dir=tmp_path,
        accessory_device_token=TOKEN,
    ))

    def opener(request, timeout):
        assert 0 < timeout <= 5
        response = client.post(
            "/v1/accessories/events",
            content=request.data,
            headers=dict(request.header_items()),
        )
        if response.status_code >= 400:
            raise urllib.error.HTTPError(
                request.full_url,
                response.status_code,
                "relay rejected",
                {},
                io.BytesIO(response.content),
            )
        return RelayResponse(response)

    module = load_relay_module()
    relay = module.AccessoryHttpRelay(
        "https://relay.example/v1/accessories/events",
        TOKEN,
        "contract-boot",
        opener=opener,
    )
    assert relay.deliver({"detected": True})
    assert relay.deliver({"detected": False})

    events = client.get(
        "/v1/accessories/events?after=0",
        headers={"Authorization": "Bearer " + TOKEN},
    ).json()["events"]
    assert [event["value"] for event in events] == [True, False]
    assert [event["sequence"] for event in events] == [1, 2]
    assert all(event["device_id"] == "mmwave_bridge" for event in events)
    assert all(event["session_id"] == "contract-boot" for event in events)
