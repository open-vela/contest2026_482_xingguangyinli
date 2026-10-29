import importlib.util
import pathlib
import stat
from unittest import mock

import pytest


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "prepare_local_roadshow_secrets.py"


def load():
    spec = importlib.util.spec_from_file_location("prepare_local_roadshow_secrets", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def write_source(path):
    path.write_text(
        "LIVING_CANVAS_MODEL_BASE_URL=https://dashscope.aliyuncs.com/compatible-mode/v1\n"
        "LIVING_CANVAS_MODEL_API_KEY=test-provider-key-not-a-secret\n"
        "LIVING_CANVAS_MODEL_NAME=qwen-flash\n"
        "LIVING_CANVAS_VOICE_DEVICE_TOKEN=old-token-that-must-not-survive\n"
        "LIVING_CANVAS_ACCESSORY_DEVICE_TOKEN=old-accessory-that-must-not-survive\n",
        encoding="ascii",
    )
    path.chmod(0o600)


def test_prepares_atomic_locked_down_runtime_generation(tmp_path):
    module = load()
    source = tmp_path / "source.env"
    write_source(source)
    destination = tmp_path / "generation"
    generated = ["A" * 43, "B" * 43]

    with mock.patch.object(module.secrets, "token_urlsafe", side_effect=generated):
        result = module.prepare(source, destination)

    assert result == destination
    assert stat.S_IMODE(destination.stat().st_mode) == 0o700
    assert {path.name for path in destination.iterdir()} == {
        "backend.env", "owner-token", "accessory-token"
    }
    for path in destination.iterdir():
        assert stat.S_IMODE(path.stat().st_mode) == 0o600
    assert (destination / "owner-token").read_text(encoding="ascii") == \
        "lc-owner-" + generated[0] + "\n"
    assert (destination / "accessory-token").read_text(encoding="ascii") == \
        "lc-accessory-" + generated[1] + "\n"
    environment = (destination / "backend.env").read_text(encoding="ascii")
    assert "old-token" not in environment
    assert "old-accessory" not in environment
    assert "LIVING_CANVAS_MODEL_API_KEY=test-provider-key-not-a-secret\n" in environment
    assert "LIVING_CANVAS_VOICE_API_KEY=" not in environment
    assert "LIVING_CANVAS_VOICE_DEVICE_TOKEN=lc-owner-" in environment
    assert "LIVING_CANVAS_ACCESSORY_DEVICE_TOKEN=lc-accessory-" in environment


@pytest.mark.parametrize("mutation", ["relative", "existing", "unsafe-parent"])
def test_rejects_unsafe_destination(tmp_path, mutation):
    module = load()
    source = tmp_path / "source.env"
    write_source(source)
    destination = tmp_path / "generation"
    if mutation == "relative":
        destination = pathlib.Path("generation")
    elif mutation == "existing":
        destination.mkdir()
    elif mutation == "unsafe-parent":
        parent = tmp_path / "public"
        parent.mkdir(mode=0o755)
        destination = parent / "generation"

    with pytest.raises(module.PreparationError, match="invalid_output_directory"):
        module.prepare(source, destination)


def test_rejects_reused_generated_tokens(tmp_path):
    module = load()
    source = tmp_path / "source.env"
    write_source(source)
    with mock.patch.object(module.secrets, "token_urlsafe", return_value="A" * 43):
        with pytest.raises(module.PreparationError, match="token_generation_failed"):
            module.prepare(source, tmp_path / "generation")
