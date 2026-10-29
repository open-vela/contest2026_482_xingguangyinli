import httpx
from fastapi.testclient import TestClient

from living_canvas_backend.app import create_app
from living_canvas_backend.model_gateway import ModelGateway
from living_canvas_backend.voice_gateway import QwenVoiceGateway


OWNER_TOKEN = "lc-owner-synthetic-readiness-token-0123456789"
ACCESSORY_TOKEN = "lc-accessory-synthetic-readiness-token-0123456789"


def model_gateway(enabled=True):
    return ModelGateway(
        "https://api.xiaomimomo.invalid/v1" if not enabled
        else "https://api.xiaomimimo.com/v1",
        "mimo-v2.6-flash",
        "synthetic-model-key",
        client=httpx.Client(transport=httpx.MockTransport(
            lambda _request: httpx.Response(500)
        )),
    )


def voice_gateway(enabled=True):
    return QwenVoiceGateway(
        "synthetic-voice-key" if enabled else "",
        client=httpx.Client(transport=httpx.MockTransport(
            lambda _request: httpx.Response(500)
        )),
    )


def test_internal_readiness_requires_every_roadshow_trust_boundary(tmp_path):
    cases = (
        {"model_gateway": None},
        {"model_gateway": model_gateway(False)},
        {"voice_gateway": voice_gateway(False)},
        {"voice_device_token": ""},
        {"accessory_device_token": ""},
        {"accessory_device_token": "accessory token with spaces is unsafe"},
        {"accessory_device_token": "x" * 129},
        {"accessory_device_token": OWNER_TOKEN},
    )
    complete = {
        "model_gateway": model_gateway(),
        "voice_gateway": voice_gateway(),
        "voice_device_token": OWNER_TOKEN,
        "accessory_device_token": ACCESSORY_TOKEN,
    }

    for index, override in enumerate(cases):
        configured = {**complete, **override}
        response = TestClient(create_app(
            data_dir=tmp_path / str(index), **configured
        )).get("/internal/readiness")
        assert response.status_code == 503
        assert response.json() == {"detail": "service_not_ready"}


def test_internal_readiness_is_safe_and_does_not_call_providers(tmp_path):
    response = TestClient(create_app(
        data_dir=tmp_path,
        model_gateway=model_gateway(),
        voice_gateway=voice_gateway(),
        voice_device_token=OWNER_TOKEN,
        accessory_device_token=ACCESSORY_TOKEN,
    )).get("/internal/readiness")

    assert response.status_code == 200
    assert response.json() == {"status": "ready"}
    assert OWNER_TOKEN not in response.text
    assert ACCESSORY_TOKEN not in response.text
