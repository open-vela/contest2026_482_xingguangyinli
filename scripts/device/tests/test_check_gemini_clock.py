"""Real preflight/clock tests; inject only ADB, never contact a physical board."""
import importlib.util
import json
import os
import subprocess
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

import pytest

DEVICE_DIR = Path(__file__).resolve().parents[1]
PREFLIGHT = DEVICE_DIR / "check_gemini_runtime.sh"
CLOCK_CHECK = DEVICE_DIR / "check_gemini_clock.py"
REFERENCE = datetime(2026, 10, 13, 2, 0, tzinfo=timezone.utc)


def iso(value):
    return value.strftime("%Y-%m-%dT%H:%M:%SZ")


def load_clock():
    assert CLOCK_CHECK.is_file(), "UTC clock gate is not implemented"
    spec = importlib.util.spec_from_file_location("gemini_clock", CLOCK_CHECK)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


@pytest.mark.parametrize("skew", [-601, -600, -1, 0, 1, 600, 601])
def test_clock_gate_uses_bounded_utc_skew(skew):
    clock_module = load_clock()
    result = clock_module.check_clock(iso(REFERENCE + timedelta(seconds=skew)), REFERENCE)
    assert result["status"] == ("PASS" if abs(skew) <= 600 else "FAIL")
    assert result["skew_seconds"] == skew
    assert result["max_skew_seconds"] == 600
    assert result["reason"] == ("within_window" if abs(skew) <= 600 else "clock_skew")


@pytest.mark.parametrize("raw", ["", "not a date", "2026-02-30T02:00:00Z",
    "2026-10-13T02:00:00", "2026-10-13T02:00:00+08:00",
    "2026-10-13T02:00:00Z\nsecond line"])
def test_clock_gate_rejects_missing_invalid_or_ambiguous_utc(raw):
    clock_module = load_clock()
    result = clock_module.check_clock(raw, REFERENCE)
    assert result["status"] == "FAIL"
    assert result["reason"] == "unparseable_board_utc"


def test_invalid_host_clock_cannot_make_1970_board_pass():
    clock_module = load_clock()
    result = clock_module.check_clock("1970-01-01T00:00:00Z",
        datetime(1970, 1, 1, tzinfo=timezone.utc))
    assert result["status"] == "FAIL"
    assert result["reason"] == "unusable_reference_clock"
    result = clock_module.check_clock(iso(REFERENCE), REFERENCE.replace(tzinfo=None))
    assert result["status"] == "FAIL"


@pytest.fixture
def adb(tmp_path):
    path = tmp_path / "adb"
    path.write_text('''#!/usr/bin/env bash
printf '%s\\n' "$*" >>"$ADB_CALL_LOG"
case "$*" in
  'devices -l') printf 'List of devices attached\\n1234 device\\n' ;;
  '-s 1234 shell ps') printf 'mediad\\nai_agent\\nliving_canvas --product\\n' ;;
  '-s 1234 shell cat /data/living_canvas/backend_url') printf 'https://api.example.invalid\\n' ;;
  '-s 1234 shell date'|'-s 1234 shell date -u +%Y-%m-%dT%H:%M:%SZ') printf '%s\\n' "$FAKE_BOARD_DATE" ;;
  *) exit 90 ;;
esac
''', encoding="utf-8")
    path.chmod(0o700)
    return path


@pytest.mark.parametrize("case", ["current", "crlf", "epoch", "yesterday", "tomorrow", "garbage",
    "split-line", "embedded-cr"])
def test_real_runtime_preflight_includes_clock_in_overall(adb, tmp_path, case):
    now = datetime.now(timezone.utc)
    raw = {"current": iso(now), "crlf": iso(now) + "\r", "epoch": "1970-01-01T00:00:00Z",
        "yesterday": iso(now - timedelta(days=1)), "tomorrow": iso(now + timedelta(days=1)),
        "garbage": "unavailable", "split-line": iso(now)[:11] + "\n" + iso(now)[11:],
        "embedded-cr": iso(now)[:11] + "\r" + iso(now)[11:]}[case]
    env = {"PATH": os.environ["PATH"], "LC_ALL": "C", "FAKE_BOARD_DATE": raw,
        "ADB_CALL_LOG": str(tmp_path / "calls"), "LC_CLOCK_PYTHON": sys.executable}
    result = subprocess.run(["bash", str(PREFLIGHT), "--adb", str(adb),
        "--evidence-dir", str(tmp_path / "evidence")], env=env,
        capture_output=True, text=True, timeout=10)
    valid = case in ("current", "crlf")
    assert result.returncode == (0 if valid else 1), result.stdout + result.stderr
    evidence = list((tmp_path / "evidence").glob("gemini-runtime-preflight-*.txt"))
    assert len(evidence) == 1
    text = evidence[0].read_text()
    expected = "PASS" if valid else "FAIL"
    assert "Overall: " + expected in text
    assert "Scope: runtime/config only; service reachability is not asserted" in text
    assert "backend_reachability: NOT_CHECKED" in text
    assert "clock: " + expected in text
    assert "Mutations: NONE" in text
    calls = (tmp_path / "calls").read_text().splitlines()
    assert calls == ["devices -l", "-s 1234 shell ps",
        "-s 1234 shell cat /data/living_canvas/backend_url",
        "-s 1234 shell date -u +%Y-%m-%dT%H:%M:%SZ"]


@pytest.mark.parametrize("interpreter", ["/nonexistent/lc-clock-python", "false"])
def test_runtime_fails_closed_if_clock_checker_cannot_run(adb, tmp_path, interpreter):
    env = {"PATH": os.environ["PATH"], "LC_ALL": "C",
        "FAKE_BOARD_DATE": iso(datetime.now(timezone.utc)),
        "ADB_CALL_LOG": str(tmp_path / "calls"), "LC_CLOCK_PYTHON": interpreter}
    result = subprocess.run(["bash", str(PREFLIGHT), "--adb", str(adb),
        "--evidence-dir", str(tmp_path / "evidence")], env=env,
        capture_output=True, text=True, timeout=10)
    assert result.returncode == 1
    evidence = next((tmp_path / "evidence").glob("gemini-runtime-preflight-*.txt")).read_text()
    assert "Overall: FAIL" in evidence
    assert "clock: FAIL" in evidence
    report = json.loads(next(line.split(": ", 1)[1] for line in evidence.splitlines()
        if line.startswith("clock_check: ")))
    assert report["reason"] == "clock_check_unavailable"


def test_clock_cli_returns_failure_without_raw_error_data():
    load_clock()
    result = subprocess.run([sys.executable, str(CLOCK_CHECK),
        "--board-date", "unavailable"], capture_output=True, text=True, timeout=5)
    assert result.returncode == 1
    report = json.loads(result.stdout)
    assert report["status"] == "FAIL"
    assert report["reason"] == "unparseable_board_utc"
    assert "unavailable" not in result.stdout
    assert result.stderr == ""


def test_runtime_preflight_fails_promptly_when_adb_hangs(tmp_path):
    adb = tmp_path / "adb"
    adb.write_text("#!/bin/sh\nsleep 30\n", encoding="utf-8")
    adb.chmod(0o700)
    env = {"PATH": os.environ["PATH"], "LC_ALL": "C",
        "LC_ADB_TIMEOUT_SECONDS": "0.2", "LC_BOUNDED_PYTHON": sys.executable}
    started = datetime.now(timezone.utc)
    result = subprocess.run(["bash", str(PREFLIGHT), "--adb", str(adb),
        "--evidence-dir", str(tmp_path / "evidence")], env=env,
        capture_output=True, text=True, timeout=5)
    elapsed = (datetime.now(timezone.utc) - started).total_seconds()
    assert result.returncode != 0
    assert elapsed < 2
    assert "timed out" in result.stderr.lower()


def test_runtime_preflight_records_other_checks_when_ps_hangs(tmp_path):
    adb = tmp_path / "adb"
    adb.write_text('''#!/bin/sh
printf '%s\\n' "$*" >>"$ADB_CALL_LOG"
case "$*" in
  'devices -l') printf 'List of devices attached\\n1234 device\\n' ;;
  '-s 1234 shell ps') sleep 30 ;;
  '-s 1234 shell cat /data/living_canvas/backend_url') printf 'https://api.example.invalid\\n' ;;
  '-s 1234 shell date -u +%Y-%m-%dT%H:%M:%SZ') printf '%s\\n' "$FAKE_BOARD_DATE" ;;
  *) exit 90 ;;
esac
''', encoding="utf-8")
    adb.chmod(0o700)
    # A freshly created executable can take more than one second to pass the
    # first macOS launch-policy scan on a cold test worker. Keep the simulated
    # ps command far beyond the deadline while allowing that one-time startup.
    env = {"PATH": os.environ["PATH"], "LC_ALL": "C",
        "LC_ADB_TIMEOUT_SECONDS": "2", "LC_BOUNDED_PYTHON": sys.executable,
        "LC_CLOCK_PYTHON": sys.executable,
        "FAKE_BOARD_DATE": iso(datetime.now(timezone.utc)),
        "ADB_CALL_LOG": str(tmp_path / "calls")}
    # Execute the temporary shim once outside the one-second-style product
    # deadline. macOS can hold the first launch of a brand-new executable for
    # endpoint-policy inspection; that is unrelated to the simulated ps hang.
    subprocess.run([str(adb), "devices", "-l"], env=env, check=True,
        capture_output=True, text=True, timeout=5)
    Path(env["ADB_CALL_LOG"]).unlink()
    result = subprocess.run(["bash", str(PREFLIGHT), "--adb", str(adb),
        "--evidence-dir", str(tmp_path / "evidence")], env=env,
        capture_output=True, text=True, timeout=10)
    assert result.returncode == 1
    evidence_files = list((tmp_path / "evidence").glob(
        "gemini-runtime-preflight-*.txt"))
    assert len(evidence_files) == 1, result.stdout + result.stderr
    evidence = evidence_files[0].read_text()
    assert "mediad: FAIL" in evidence
    assert "ai_agent: FAIL" in evidence
    assert "living_canvas --product: FAIL" in evidence
    assert "backend_url: PASS" in evidence
    assert "clock: PASS" in evidence
    assert (tmp_path / "calls").read_text().splitlines() == [
        "devices -l", "-s 1234 shell ps",
        "-s 1234 shell cat /data/living_canvas/backend_url",
        "-s 1234 shell date -u +%Y-%m-%dT%H:%M:%SZ"]
