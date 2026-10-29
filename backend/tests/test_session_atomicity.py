from concurrent.futures import ThreadPoolExecutor
from threading import Event

from fastapi.testclient import TestClient

from living_canvas_backend.app import create_app
from test_api_flow import create_session, valid_input


OWNER_TOKEN = "lc-owner-atomic-device-token-0123456789abcdef"


class BlockingModelGateway:
    def __init__(self):
        self.started = Event()
        self.release = Event()

    def choose(self, candidates, preferences):
        self.started.set()
        if not self.release.wait(timeout=5):
            raise RuntimeError("test model gate timed out")
        return None


def atomic_client(tmp_path, gateway):
    return TestClient(
        create_app(
            data_dir=tmp_path,
            model_gateway=gateway,
            voice_device_token=OWNER_TOKEN,
        ),
        headers={"Authorization": "Bearer " + OWNER_TOKEN},
    )


def test_cancel_during_slow_input_cannot_be_overwritten(tmp_path):
    gateway = BlockingModelGateway()
    client = atomic_client(tmp_path, gateway)
    session_id = create_session(client)

    with ThreadPoolExecutor(max_workers=1) as executor:
        future = executor.submit(
            client.post, "/v1/input", json=valid_input(session_id)
        )
        assert gateway.started.wait(timeout=5)
        cancelled = client.post("/v1/device/event", json={
            "session_id": session_id, "event": "cancel",
        })
        gateway.release.set()
        response = future.result(timeout=5)

    assert cancelled.status_code == 200
    assert response.status_code == 409
    assert response.json()["detail"] == "input_superseded"
    assert client.get("/v1/session/" + session_id).json()["state"] == "cancelled"


def test_cancel_during_slow_dialogue_cannot_be_overwritten(tmp_path):
    gateway = BlockingModelGateway()
    client = atomic_client(tmp_path, gateway)
    session_id = create_session(client)

    with ThreadPoolExecutor(max_workers=1) as executor:
        future = executor.submit(client.post, "/v1/dialogue", json={
            "session_id": session_id,
            "choice": "takeout",
            "transcript": "帮我推荐晚餐",
        })
        assert gateway.started.wait(timeout=5)
        cancelled = client.post("/v1/device/event", json={
            "session_id": session_id, "event": "cancel",
        })
        gateway.release.set()
        response = future.result(timeout=5)

    assert cancelled.status_code == 200
    assert response.status_code == 409
    assert response.json()["detail"] == "dialogue_not_allowed"
    assert client.get("/v1/session/" + session_id).json()["state"] == "cancelled"
