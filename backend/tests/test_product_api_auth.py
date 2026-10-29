from fastapi.testclient import TestClient

from living_canvas_backend.app import create_app


TOKEN = "lc-owner-test-device-token-0123456789abcdef"
AUTH = {"Authorization": "Bearer " + TOKEN}
PROTECTED_WRITES = (
    ("/v1/session", {"device_id": "frame-a"}),
    ("/v1/input", {}),
    ("/v1/dialogue", {}),
    ("/v1/device/event", {}),
    ("/v1/confirm", {}),
)
PROTECTED_MEMORY_REQUESTS = (
    ("POST", "/v1/feedback", {
        "session_id": "meal_000000000000",
        "liked": True,
        "note": "synthetic",
    }),
    ("GET", "/v1/memory", None),
    ("DELETE", "/v1/memory", None),
)


class CountingModel:
    def __init__(self):
        self.calls = 0

    def choose(self, _candidates, _preferences):
        self.calls += 1
        return None


def test_all_product_writes_reject_missing_and_wrong_bearer_before_body_validation(tmp_path):
    client = TestClient(create_app(data_dir=tmp_path, voice_device_token=TOKEN))

    for path, body in PROTECTED_WRITES:
        missing = client.post(path, json=body)
        wrong = client.post(
            path,
            json=body,
            headers={"Authorization": "Bearer wrong-owner-device-token"},
        )
        assert missing.status_code == 401, (path, missing.text)
        assert missing.json()["detail"] == "product_device_unauthorized"
        assert wrong.status_code == 401, (path, wrong.text)
        assert wrong.json()["detail"] == "product_device_unauthorized"


def test_all_product_writes_fail_closed_when_owner_token_is_not_configured(tmp_path):
    client = TestClient(create_app(data_dir=tmp_path, voice_device_token=""))

    for path, body in PROTECTED_WRITES:
        response = client.post(path, json=body)
        assert response.status_code == 503, (path, response.text)
        assert response.json()["detail"] == "product_device_not_configured"


def test_feedback_and_memory_reject_missing_and_wrong_owner_bearer(tmp_path):
    client = TestClient(create_app(data_dir=tmp_path, voice_device_token=TOKEN))

    for method, path, body in PROTECTED_MEMORY_REQUESTS:
        missing = client.request(method, path, json=body)
        wrong = client.request(
            method,
            path,
            json=body,
            headers={"Authorization": "Bearer wrong-owner-device-token"},
        )
        assert missing.status_code == 401, (method, path, missing.text)
        assert missing.json()["detail"] == "product_device_unauthorized"
        assert wrong.status_code == 401, (method, path, wrong.text)
        assert wrong.json()["detail"] == "product_device_unauthorized"


def test_feedback_and_memory_fail_closed_without_owner_token(tmp_path):
    client = TestClient(create_app(data_dir=tmp_path, voice_device_token=""))

    for method, path, body in PROTECTED_MEMORY_REQUESTS:
        response = client.request(method, path, json=body)
        assert response.status_code == 503, (method, path, response.text)
        assert response.json()["detail"] == "product_device_not_configured"


def test_legacy_unprefixed_owner_token_fails_closed_for_atomic_rotation(tmp_path):
    legacy = "test-owner-device-token-0123456789abcdef"
    client = TestClient(create_app(data_dir=tmp_path, voice_device_token=legacy))
    response = client.post(
        "/v1/session",
        json={"device_id": "frame-a"},
        headers={"Authorization": "Bearer " + legacy},
    )
    assert response.status_code == 503
    assert response.json()["detail"] == "product_device_not_configured"


def test_unauthorized_request_neither_creates_session_nor_calls_model(tmp_path):
    model = CountingModel()
    app = create_app(
        data_dir=tmp_path,
        model_gateway=model,
        voice_device_token=TOKEN,
    )
    client = TestClient(app)

    denied = client.post("/v1/session", json={"device_id": "frame-a"})
    assert denied.status_code == 401
    assert len(app.state.sessions._sessions) == 0
    assert model.calls == 0


def test_correct_owner_bearer_preserves_product_flow(tmp_path):
    model = CountingModel()
    client = TestClient(
        create_app(
            data_dir=tmp_path,
            model_gateway=model,
            voice_device_token=TOKEN,
        )
    )

    created = client.post(
        "/v1/session", json={"device_id": "frame-a"}, headers=AUTH
    )
    assert created.status_code == 201
    session_id = created.json()["session_id"]

    recommended = client.post(
        "/v1/dialogue",
        json={
            "session_id": session_id,
            "choice": "takeout",
            "transcript": "想吃热的中餐",
        },
        headers=AUTH,
    )
    assert recommended.status_code == 200
    assert recommended.json()["state"] == "candidate"
    assert model.calls == 1
