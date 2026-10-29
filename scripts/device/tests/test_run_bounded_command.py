import importlib.util
import os
import subprocess
import sys
import time
from pathlib import Path


DEVICE_DIR = Path(__file__).resolve().parents[1]
RUNNER = DEVICE_DIR / "run_bounded_command.py"


def load_runner():
    assert RUNNER.is_file(), "bounded command runner is not implemented"
    spec = importlib.util.spec_from_file_location("bounded_command", RUNNER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_forwards_stdout_stderr_and_exit_status():
    load_runner()
    result = subprocess.run(
        [sys.executable, str(RUNNER), "--timeout", "2", "--",
         sys.executable, "-c",
         "import sys; print('out'); print('err', file=sys.stderr); raise SystemExit(7)"],
        capture_output=True, text=True, timeout=5)
    assert result.returncode == 7
    assert result.stdout == "out\n"
    assert result.stderr == "err\n"


def test_timeout_terminates_the_process_group_promptly(tmp_path):
    load_runner()
    marker = tmp_path / "child-survived"
    child_code = (
        "import pathlib,time; time.sleep(1); "
        f"pathlib.Path({str(marker)!r}).write_text('bad')"
    )
    parent_code = (
        "import subprocess,sys,time; "
        f"subprocess.Popen([sys.executable,'-c',{child_code!r}]); "
        "time.sleep(30)"
    )
    started = time.monotonic()
    result = subprocess.run(
        [sys.executable, str(RUNNER), "--timeout", "0.2", "--",
         sys.executable, "-c", parent_code],
        capture_output=True, text=True, timeout=5)
    elapsed = time.monotonic() - started
    assert result.returncode == 124
    assert elapsed < 2
    time.sleep(1.1)
    assert not marker.exists()
    assert "timed out" in result.stderr.lower()


def test_rejects_invalid_timeout_without_running_command(tmp_path):
    marker = tmp_path / "ran"
    result = subprocess.run(
        [sys.executable, str(RUNNER), "--timeout", "0", "--",
         sys.executable, "-c", f"open({str(marker)!r},'w').close()"],
        capture_output=True, text=True, timeout=5)
    assert result.returncode == 64
    assert not marker.exists()
