import pytest
from fastapi.testclient import TestClient

from living_canvas_backend.app import create_app


TEST_OWNER_DEVICE_TOKEN = "lc-owner-test-device-token-0123456789abcdef"


@pytest.fixture
def client(tmp_path):
    return TestClient(
        create_app(data_dir=tmp_path, voice_device_token=TEST_OWNER_DEVICE_TOKEN),
        headers={"Authorization": "Bearer " + TEST_OWNER_DEVICE_TOKEN},
    )
