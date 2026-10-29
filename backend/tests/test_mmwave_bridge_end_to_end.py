"""Host proof for the privacy-minimal MR60 -> backend bridge seam."""

import importlib.util
import io
import json
import urllib.error
from pathlib import Path

from fastapi.testclient import TestClient

from living_canvas_backend.app import create_app


TOKEN = "synthetic-accessory-token-at-least-32-characters"
REPO = Path(__file__).resolve().parents[2]
TOOLS = REPO / "app" / "hello_app" / "tools"


def load(name: str):
    path = TOOLS / f"{name}.py"
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


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


def test_validated_target_counts_reach_backend_as_booleans_only(tmp_path):
    presence_filter = load("esphome_presence_filter")
    relay_module = load("accessory_http_relay")
    client = TestClient(create_app(
        data_dir=tmp_path,
        accessory_device_token=TOKEN,
    ))

    # A distance line must never become an event. Only complete target-count
    # lines are permitted to cross the privacy boundary.
    raw = io.BytesIO(
        b"[D][sensor:001]: 'Target distance': Sending state 73.0 cm\n"
        b"[D][sensor:001]: 'Target number': Sending state 1.00000 with 5 decimals of accuracy\n"
        b"[D][sensor:001]: 'Target number': Sending state 0.00000 with 5 decimals of accuracy\n"
    )
    filtered = io.StringIO()
    errors = io.StringIO()
    assert presence_filter.filter_stream(raw, filtered, errors) == 0
    assert errors.getvalue() == ""
    assert filtered.getvalue() == '{"detected":true}\n{"detected":false}\n'

    def opener(request, timeout):
        assert 0 < timeout <= 5
        response = client.post(
            "/v1/accessories/events",
            content=request.data,
            headers=dict(request.header_items()),
        )
        if response.status_code >= 400:
            raise urllib.error.HTTPError(
                request.full_url, response.status_code, "relay rejected", {},
                io.BytesIO(response.content))
        return RelayResponse(response)

    relay = relay_module.AccessoryHttpRelay(
        "https://relay.example/v1/accessories/events",
        TOKEN,
        "mmwave-e2e",
        opener=opener,
    )
    assert relay_module.relay_stream(
        io.BytesIO(filtered.getvalue().encode("ascii")), relay, errors) == 0

    payload = client.get(
        "/v1/accessories/events?after=0",
        headers={"Authorization": "Bearer " + TOKEN},
    ).json()
    assert payload["next_cursor"] == 2
    assert payload["events"] == [
        {
            "cursor": 1,
            "device_id": "mmwave_bridge",
            "session_id": "mmwave-e2e",
            "sequence": 1,
            "event": "presence",
            "value": True,
        },
        {
            "cursor": 2,
            "device_id": "mmwave_bridge",
            "session_id": "mmwave-e2e",
            "sequence": 2,
            "event": "presence",
            "value": False,
        },
    ]
    # Prove no raw sensor line or extra measurement crossed the HTTP seam.
    encoded = json.dumps(payload, ensure_ascii=True)
    assert "distance" not in encoded.lower()
    assert "73" not in encoded
