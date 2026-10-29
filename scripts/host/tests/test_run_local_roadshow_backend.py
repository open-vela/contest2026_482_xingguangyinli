import contextlib
import importlib.util
import io
import os
import pathlib
import stat
import sys
from unittest import mock

import pytest


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "run_local_roadshow_backend.py"


def load():
    spec = importlib.util.spec_from_file_location("run_local_roadshow_backend", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def safe_environment():
    return {
        "LIVING_CANVAS_MODEL_BASE_URL": "https://api.xiaomimimo.com/v1",
        "LIVING_CANVAS_MODEL_API_KEY": "test-model-key-not-a-secret",
        "LIVING_CANVAS_MODEL_NAME": "mimo-v2.6-flash",
        "LIVING_CANVAS_VOICE_API_KEY": "synthetic-voice-key-for-tests",
        "LIVING_CANVAS_VOICE_DEVICE_TOKEN":
            "lc-owner-synthetic-device-token-32-characters",
        "LIVING_CANVAS_ACCESSORY_DEVICE_TOKEN":
            "lc-accessory-synthetic-device-token-32-characters",
    }


def existing_dashscope_environment():
    environment = safe_environment()
    environment.update({
        "LIVING_CANVAS_MODEL_BASE_URL":
            "https://dashscope.aliyuncs.com/compatible-mode/v1",
        "LIVING_CANVAS_MODEL_NAME": "qwen-flash",
        "LIVING_CANVAS_MODEL_API_KEY": "test-shared-provider-key-not-a-secret",
    })
    environment.pop("LIVING_CANVAS_VOICE_API_KEY")
    return environment


@pytest.mark.parametrize("host", [
    "127.0.0.1", "8.8.8.8", "169.254.1.1", "224.0.0.1",
    "192.0.2.1", "example.test", "0.0.0.0",
])
def test_rejects_non_rfc1918_bind_addresses(host, tmp_path):
    module = load()
    with pytest.raises(module.ConfigurationError, match="invalid_private_host"):
        module.configure(host, 8765, tmp_path, safe_environment())


@pytest.mark.parametrize("host", ["10.42.0.1", "172.20.10.1", "192.168.8.20"])
def test_accepts_only_explicit_rfc1918_address_and_builds_local_origin(host, tmp_path):
    module = load()
    configured = module.configure(host, 8765, tmp_path, safe_environment())
    assert configured["LIVING_CANVAS_HOST"] == host
    assert configured["LIVING_CANVAS_PORT"] == "8765"
    assert configured["LIVING_CANVAS_PUBLIC_BASE_URL"] == f"http://{host}:8765"
    assert configured["LIVING_CANVAS_DATA_DIR"] == str(tmp_path.resolve())
    assert stat.S_IMODE(tmp_path.stat().st_mode) == 0o700


@pytest.mark.parametrize("missing", [
    "LIVING_CANVAS_MODEL_BASE_URL", "LIVING_CANVAS_MODEL_API_KEY",
    "LIVING_CANVAS_MODEL_NAME", "LIVING_CANVAS_VOICE_API_KEY",
    "LIVING_CANVAS_VOICE_DEVICE_TOKEN",
    "LIVING_CANVAS_ACCESSORY_DEVICE_TOKEN",
])
def test_refuses_to_start_when_real_ai_or_voice_config_is_missing(missing, tmp_path):
    module = load()
    environment = safe_environment()
    environment[missing] = ""
    with pytest.raises(module.ConfigurationError, match="configuration_missing"):
        module.configure("192.168.8.20", 8765, tmp_path, environment)


def test_rejects_provider_key_in_owner_device_token_slot(tmp_path):
    module = load()
    environment = safe_environment()
    environment["LIVING_CANVAS_VOICE_DEVICE_TOKEN"] = "s" + "k-" + "x" * 40
    with pytest.raises(module.ConfigurationError, match="invalid_device_token"):
        module.configure("192.168.8.20", 8765, tmp_path, environment)


@pytest.mark.parametrize("provider_name", [
    "LIVING_CANVAS_MODEL_API_KEY", "LIVING_CANVAS_VOICE_API_KEY",
])
def test_rejects_owner_device_token_equal_to_any_provider_key(
        provider_name, tmp_path):
    module = load()
    environment = safe_environment()
    provider_value = "lc-owner-provider-shaped-secret-0123456789"
    environment[provider_name] = provider_value
    environment["LIVING_CANVAS_VOICE_DEVICE_TOKEN"] = provider_value
    with pytest.raises(module.ConfigurationError, match="invalid_device_token"):
        module.configure("192.168.8.20", 8765, tmp_path, environment)


def test_rejects_owner_device_token_without_product_prefix(tmp_path):
    module = load()
    environment = safe_environment()
    environment["LIVING_CANVAS_VOICE_DEVICE_TOKEN"] = \
        "synthetic-owner-device-token-32-characters"
    with pytest.raises(module.ConfigurationError, match="invalid_device_token"):
        module.configure("192.168.8.20", 8765, tmp_path, environment)


@pytest.mark.parametrize("value", [
    "short",
    "s" + "k-" + "x" * 40,
    "accessory token with spaces is unsafe",
    "x" * 129,
])
def test_rejects_accessory_token_that_s1_cannot_load(value, tmp_path):
    module = load()
    environment = safe_environment()
    environment["LIVING_CANVAS_ACCESSORY_DEVICE_TOKEN"] = value
    with pytest.raises(module.ConfigurationError, match="invalid_accessory_token"):
        module.configure("192.168.8.20", 8765, tmp_path, environment)


@pytest.mark.parametrize("other_name", [
    "LIVING_CANVAS_MODEL_API_KEY",
    "LIVING_CANVAS_VOICE_API_KEY",
    "LIVING_CANVAS_VOICE_DEVICE_TOKEN",
])
def test_rejects_accessory_token_reused_for_another_trust_boundary(
        other_name, tmp_path):
    module = load()
    environment = safe_environment()
    environment["LIVING_CANVAS_ACCESSORY_DEVICE_TOKEN"] = environment[other_name]
    with pytest.raises(module.ConfigurationError, match="invalid_accessory_token"):
        module.configure("192.168.8.20", 8765, tmp_path, environment)


def test_accepts_owner_device_token_from_private_file(tmp_path):
    module = load()
    environment = safe_environment()
    token = environment.pop("LIVING_CANVAS_VOICE_DEVICE_TOKEN")
    token_file = tmp_path / "owner-token"
    token_file.write_text(token + "\n", encoding="ascii")
    token_file.chmod(0o600)
    environment["LIVING_CANVAS_VOICE_DEVICE_TOKEN_FILE"] = str(token_file)
    data_dir = tmp_path / "data"

    configured = module.configure(
        "192.168.8.20", 8765, data_dir, environment
    )

    assert configured["LIVING_CANVAS_VOICE_DEVICE_TOKEN"] == token
    assert "LIVING_CANVAS_VOICE_DEVICE_TOKEN_FILE" not in configured


def test_rejects_ambiguous_direct_and_file_device_tokens(tmp_path):
    module = load()
    environment = safe_environment()
    token_file = tmp_path / "owner-token"
    token_file.write_text(
        environment["LIVING_CANVAS_VOICE_DEVICE_TOKEN"] + "\n",
        encoding="ascii",
    )
    token_file.chmod(0o600)
    environment["LIVING_CANVAS_VOICE_DEVICE_TOKEN_FILE"] = str(token_file)
    with pytest.raises(module.ConfigurationError, match="ambiguous_device_token"):
        module.configure("192.168.8.20", 8765, tmp_path / "data", environment)


@pytest.mark.parametrize("mutation", ["relative", "symlink", "permissions", "multiline"])
def test_rejects_unsafe_owner_device_token_file(tmp_path, mutation):
    module = load()
    environment = safe_environment()
    token = environment.pop("LIVING_CANVAS_VOICE_DEVICE_TOKEN")
    token_file = tmp_path / "owner-token"
    token_file.write_text(token + "\n", encoding="ascii")
    token_file.chmod(0o600)
    selected = token_file
    if mutation == "relative":
        selected = pathlib.Path("owner-token")
    elif mutation == "symlink":
        selected = tmp_path / "owner-token-link"
        selected.symlink_to(token_file)
    elif mutation == "permissions":
        token_file.chmod(0o640)
    elif mutation == "multiline":
        token_file.write_text(token + "\nsecond-line\n", encoding="ascii")
    environment["LIVING_CANVAS_VOICE_DEVICE_TOKEN_FILE"] = str(selected)

    with pytest.raises(module.ConfigurationError, match="invalid_device_token_file"):
        module.configure("192.168.8.20", 8765, tmp_path / "data", environment)


def test_rejects_a_different_model_endpoint_that_gateway_would_disable(tmp_path):
    module = load()
    environment = safe_environment()
    environment["LIVING_CANVAS_MODEL_BASE_URL"] = "https://model.example.invalid/v1"
    with pytest.raises(module.ConfigurationError, match="invalid_model_url"):
        module.configure("192.168.8.20", 8765, tmp_path, environment)


@pytest.mark.parametrize(
    "model_name",
    ["mimo-v2.5", "mimo-v2.5-pro", "synthetic-model", "mimo-v2.6-pro-ultraspeed"],
)
def test_rejects_retired_or_unapproved_model_name(model_name, tmp_path):
    module = load()
    environment = safe_environment()
    environment["LIVING_CANVAS_MODEL_NAME"] = model_name
    with pytest.raises(module.ConfigurationError, match="invalid_model_name"):
        module.configure("192.168.8.20", 8765, tmp_path, environment)


@pytest.mark.parametrize("model_name", ["mimo-v2.6-flash", "mimo-v2.6-pro"])
def test_accepts_current_pay_as_you_go_model_names(model_name, tmp_path):
    module = load()
    environment = safe_environment()
    environment["LIVING_CANVAS_MODEL_NAME"] = model_name
    configured = module.configure("192.168.8.20", 8765, tmp_path, environment)
    assert configured["LIVING_CANVAS_MODEL_NAME"] == model_name


def test_accepts_existing_dashscope_profile_and_reuses_provider_key_for_voice(
        tmp_path):
    module = load()
    environment = existing_dashscope_environment()

    configured = module.configure("192.168.8.20", 8765, tmp_path, environment)

    assert configured["LIVING_CANVAS_MODEL_NAME"] == "qwen-flash"
    assert configured["LIVING_CANVAS_VOICE_API_KEY"] == \
        environment["LIVING_CANVAS_MODEL_API_KEY"]


@pytest.mark.parametrize("model_name", ["qwen-plus", "qwen-max", "mimo-v2.6-flash"])
def test_rejects_unapproved_model_on_dashscope_profile(model_name, tmp_path):
    module = load()
    environment = existing_dashscope_environment()
    environment["LIVING_CANVAS_MODEL_NAME"] = model_name

    with pytest.raises(module.ConfigurationError, match="invalid_model_name"):
        module.configure("192.168.8.20", 8765, tmp_path, environment)


@pytest.mark.parametrize("prefix", ["tp-", "ttp-"])
def test_rejects_token_plan_key_for_product_backend(prefix, tmp_path):
    module = load()
    environment = safe_environment()
    environment["LIVING_CANVAS_MODEL_API_KEY"] = prefix + "test-not-a-real-key"
    with pytest.raises(
        module.ConfigurationError,
        match="token_plan_not_permitted_for_product_backend",
    ):
        module.configure("192.168.8.20", 8765, tmp_path, environment)


def test_rejects_symlink_data_directory(tmp_path):
    module = load()
    real_directory = tmp_path / "real"
    real_directory.mkdir(mode=0o700)
    link = tmp_path / "linked"
    link.symlink_to(real_directory, target_is_directory=True)
    with pytest.raises(module.ConfigurationError, match="invalid_data_dir"):
        module.configure("192.168.8.20", 8765, link, safe_environment())


def test_check_mode_never_prints_secret_values_or_starts_server(tmp_path):
    module = load()
    environment = safe_environment()
    argv = [str(SCRIPT), "--host", "192.168.8.20", "--data-dir",
            str(tmp_path), "--check"]
    output = io.StringIO()
    with mock.patch.dict(os.environ, environment, clear=True), \
         mock.patch.object(sys, "argv", argv), \
         mock.patch.object(module, "start_server") as start_server, \
         contextlib.redirect_stdout(output):
        assert module.main() == 0
    start_server.assert_not_called()
    rendered = output.getvalue()
    assert '"status": "PASS"' in rendered
    assert '"accessories": "CONFIGURED"' in rendered
    for secret in environment.values():
        assert secret not in rendered


def write_private_environment(path, environment):
    path.write_text(
        "\n".join(f"{key}={value}" for key, value in environment.items()) + "\n",
        encoding="ascii",
    )
    path.chmod(0o600)


def test_check_mode_accepts_locked_down_absolute_environment_file(tmp_path):
    module = load()
    environment = existing_dashscope_environment()
    environment_file = tmp_path / "backend.env"
    write_private_environment(environment_file, environment)
    data_dir = tmp_path / "data"
    argv = [str(SCRIPT), "--host", "192.168.8.20", "--data-dir",
            str(data_dir), "--env-file", str(environment_file), "--check"]
    output = io.StringIO()

    with mock.patch.dict(os.environ, {}, clear=True), \
         mock.patch.object(sys, "argv", argv), \
         mock.patch.object(module, "start_server") as start_server, \
         contextlib.redirect_stdout(output):
        assert module.main() == 0

    start_server.assert_not_called()
    rendered = output.getvalue()
    assert '"status": "PASS"' in rendered
    for secret in environment.values():
        assert secret not in rendered


@pytest.mark.parametrize("mutation", ["relative", "symlink", "permissions", "duplicate"])
def test_rejects_unsafe_environment_file(tmp_path, mutation):
    module = load()
    environment_file = tmp_path / "backend.env"
    write_private_environment(environment_file, existing_dashscope_environment())
    selected = environment_file
    if mutation == "relative":
        selected = pathlib.Path("backend.env")
    elif mutation == "symlink":
        selected = tmp_path / "backend-link.env"
        selected.symlink_to(environment_file)
    elif mutation == "permissions":
        environment_file.chmod(0o640)
    elif mutation == "duplicate":
        with environment_file.open("a", encoding="ascii") as stream:
            stream.write("LIVING_CANVAS_MODEL_NAME=qwen-flash\n")

    with pytest.raises(module.ConfigurationError, match="invalid_environment_file"):
        module.load_private_environment_file(selected)


def test_rejects_conflicting_process_and_file_environment(tmp_path):
    module = load()
    environment_file = tmp_path / "backend.env"
    write_private_environment(environment_file, existing_dashscope_environment())

    with pytest.raises(module.ConfigurationError, match="ambiguous_environment"):
        module.merge_private_environment(
            {"LIVING_CANVAS_MODEL_NAME": "different-model"}, environment_file
        )
