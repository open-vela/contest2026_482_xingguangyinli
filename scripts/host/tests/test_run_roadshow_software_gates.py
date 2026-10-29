import importlib.util
import os
import pathlib
import subprocess
from unittest import mock


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / \
    "run_roadshow_software_gates.py"
REPO = pathlib.Path(__file__).resolve().parents[3]


def load():
    spec = importlib.util.spec_from_file_location(
        "run_roadshow_software_gates", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_plan_covers_all_host_only_roadshow_gates(tmp_path):
    module = load()
    (tmp_path / "cJSON.c").write_text("/* synthetic */")
    (tmp_path / "cJSON.h").write_text("/* synthetic */")

    sensitive = {
        "LIVING_CANVAS_MODEL_BASE_URL": "https://paid.example.invalid/v1",
        "LIVING_CANVAS_MODEL_NAME": "paid-model",
        "LIVING_CANVAS_MODEL_API_KEY": "real-model-secret",
        "LIVING_CANVAS_VOICE_API_KEY": "real-voice-secret",
        "LIVING_CANVAS_VOICE_DEVICE_TOKEN": "real-device-secret",
        "LIVING_CANVAS_ACCESSORY_DEVICE_TOKEN": "real-accessory-secret",
        "CADDY_BINARY": "/unsafe/live/caddy",
        "CADDYFILE_UNDER_TEST": "/unsafe/live/Caddyfile",
        "HTTP_PROXY": "http://private-user:private-pass@proxy.invalid:8080",
        "HTTPS_PROXY": "http://private-user:private-pass@proxy.invalid:8080",
        "ALL_PROXY": "socks5://private-user:private-pass@proxy.invalid:1080",
        "NO_PROXY": "private.internal",
        "http_proxy": "http://private-user:private-pass@proxy.invalid:8080",
        "https_proxy": "http://private-user:private-pass@proxy.invalid:8080",
        "OPENAI_API_KEY": "real-unrelated-provider-secret",
        "AWS_SECRET_ACCESS_KEY": "real-unrelated-cloud-secret",
        "GH_TOKEN": "real-unrelated-code-host-token",
        "SSH_AUTH_SOCK": "/private/tmp/agent.sock",
    }
    with mock.patch.dict(os.environ, sensitive, clear=False):
        plan = module.build_plan(
            REPO, "/safe/backend-python", tmp_path, "/safe/esp-python")

    assert [gate.name for gate in plan] == [
        "backend", "device-host", "s1-host", "esp32-host", "evidence-privacy",
        "brand-clean", "diff-check"
    ]
    rendered = "\n".join(
        " ".join(gate.command) for gate in plan).lower()
    assert "backend/tests" in rendered
    assert "scripts/host/tests" in rendered
    assert "scripts/device/tests" in rendered
    assert "app/hello_app/tests/host" in rendered
    assert "demo/esp32s3_preview/tests/run_tests.sh" in rendered
    assert "scripts/device/tests/test_evidence_privacy.sh" in rendered
    assert "tests/test_no_legacy_brand.sh" in rendered
    assert "git diff --check" in rendered
    assert plan[0].command[0] == "/safe/backend-python"
    assert plan[3].environment["PYTHON_BIN"] == "/safe/esp-python"
    for gate in plan:
        for name in sensitive:
            assert name not in gate.environment
    for forbidden in ("adb", "esptool", "ssh", "scp", "flash", "reboot"):
        assert forbidden not in rendered


def test_missing_pinned_cjson_fails_before_running_commands(tmp_path):
    module = load()
    with mock.patch.object(subprocess, "run") as run:
        assert module.main([
            "--repo", str(REPO),
            "--cjson-dir", str(tmp_path / "missing"),
        ]) == 2
    run.assert_not_called()


def test_all_gates_run_and_any_failure_fails_summary(tmp_path, capsys):
    module = load()
    (tmp_path / "cJSON.c").write_text("/* synthetic */")
    (tmp_path / "cJSON.h").write_text("/* synthetic */")
    results = [
        subprocess.CompletedProcess([], 0),
        subprocess.CompletedProcess([], 1),
        subprocess.CompletedProcess([], 0),
        subprocess.CompletedProcess([], 0),
        subprocess.CompletedProcess([], 0),
        subprocess.CompletedProcess([], 0),
        subprocess.CompletedProcess([], 0),
    ]

    with mock.patch.object(subprocess, "run", side_effect=results) as run:
        assert module.main([
            "--repo", str(REPO),
            "--python", "/safe/python",
            "--esp-python", "/safe/esp-python",
            "--cjson-dir", str(tmp_path),
        ]) == 1

    assert run.call_count == 7
    for call in run.call_args_list:
        assert call.kwargs["cwd"] == REPO
        assert call.kwargs["check"] is False
        assert call.kwargs["timeout"] == 300
        assert call.kwargs["env"]["PYTHONDONTWRITEBYTECODE"] == "1"
    output = capsys.readouterr().out
    assert "GATE backend PASS" in output
    assert "GATE device-host FAIL" in output
    assert "ROADSHOW_SOFTWARE_GATES=FAIL" in output


def test_plan_mode_does_not_execute_commands(tmp_path, capsys):
    module = load()
    (tmp_path / "cJSON.c").write_text("/* synthetic */")
    (tmp_path / "cJSON.h").write_text("/* synthetic */")
    with mock.patch.object(subprocess, "run") as run:
        assert module.main([
            "--repo", str(REPO),
            "--python", "/safe/python",
            "--esp-python", "/safe/esp-python",
            "--cjson-dir", str(tmp_path),
            "--plan",
        ]) == 0
    run.assert_not_called()
    output = capsys.readouterr().out
    assert "backend:" in output
    assert "device-host:" in output
    assert "s1-host:" in output
    assert "esp32-host:" in output
    assert "evidence-privacy:" in output
    assert "brand-clean:" in output
    assert "diff-check:" in output
    assert "ROADSHOW_SOFTWARE_GATES=NOT_RUN" in output


def test_timeout_is_reported_and_remaining_gates_still_run(tmp_path, capsys):
    module = load()
    (tmp_path / "cJSON.c").write_text("/* synthetic */")
    (tmp_path / "cJSON.h").write_text("/* synthetic */")
    results = [
        subprocess.TimeoutExpired(["backend"], 7),
        subprocess.CompletedProcess([], 0),
        subprocess.CompletedProcess([], 0),
        subprocess.CompletedProcess([], 0),
        subprocess.CompletedProcess([], 0),
        subprocess.CompletedProcess([], 0),
        subprocess.CompletedProcess([], 0),
    ]
    with mock.patch.object(subprocess, "run", side_effect=results) as run:
        assert module.main([
            "--repo", str(REPO),
            "--cjson-dir", str(tmp_path),
            "--timeout-seconds", "7",
        ]) == 1
    assert run.call_count == 7
    assert capsys.readouterr().out.count("GATE backend TIMEOUT") == 1


def test_missing_command_is_reported_and_remaining_gates_still_run(
        tmp_path, capsys):
    module = load()
    (tmp_path / "cJSON.c").write_text("/* synthetic */")
    (tmp_path / "cJSON.h").write_text("/* synthetic */")
    results = [
        FileNotFoundError("synthetic missing command"),
        subprocess.CompletedProcess([], 0),
        subprocess.CompletedProcess([], 0),
        subprocess.CompletedProcess([], 0),
        subprocess.CompletedProcess([], 0),
        subprocess.CompletedProcess([], 0),
        subprocess.CompletedProcess([], 0),
    ]
    with mock.patch.object(subprocess, "run", side_effect=results) as run:
        assert module.main([
            "--repo", str(REPO),
            "--cjson-dir", str(tmp_path),
        ]) == 1
    assert run.call_count == 7
    output = capsys.readouterr().out
    assert output.count("GATE backend ERROR") == 1
    assert "synthetic missing command" not in output
    assert "GATE device-host PASS" in output
