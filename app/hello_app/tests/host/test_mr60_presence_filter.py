"""Actual C filter -> actual JSON/LC1 bridge; only synthetic vendor frames."""
import json
import subprocess
import sys
from pathlib import Path

APP = Path(__file__).resolve().parents[2]
FILTER = APP / "tools/mr60_presence_filter.c"
PRESENT = bytes.fromhex("01 00 01 00 01 0f 09 f8 01 fe")
ABSENT = bytes.fromhex("01 00 01 00 01 0f 09 f8 00 ff")


def run_filter(tmp_path, capture, args=()):
    assert FILTER.is_file(), "vendor-to-JSON filter is missing"
    binary = tmp_path / "filter"
    compiled = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
        "-I" + str(APP / "include"), str(FILTER), str(APP / "src/lc_mr60_presence.c"),
        "-o", str(binary)], capture_output=True, text=True)
    assert compiled.returncode == 0, compiled.stderr
    return subprocess.run([str(binary), *args], input=capture, capture_output=True, timeout=5)


def test_real_filter_emits_only_boolean_json(tmp_path):
    result = run_filter(tmp_path, PRESENT + ABSENT)
    assert result.returncode == 0
    assert result.stderr == b""
    assert [json.loads(line) for line in result.stdout.splitlines()] == [
        {"detected": True}, {"detected": False}]


def test_noise_is_unknown_not_an_absence_or_raw_data_log(tmp_path):
    result = run_filter(tmp_path, b"PRIVATE_UPSTREAM_OR_VITALS" * 1000)
    assert result.returncode == 2
    assert result.stdout == b""
    assert result.stderr == b"no validated presence report\n"


def test_cli_rejects_device_paths_instead_of_opening_hardware(tmp_path):
    result = run_filter(tmp_path, b"", args=("/dev/cu.usbmodem21401",))
    assert result.returncode == 64
    assert result.stdout == b""
    assert b"usbmodem" not in result.stderr


def test_actual_filter_to_existing_lc1_bridge_is_bounded(tmp_path):
    result = run_filter(tmp_path, PRESENT + ABSENT)
    assert result.returncode == 0
    bridged = subprocess.run([sys.executable, str(APP / "tools/presence_bridge.py"),
        "--source", "mmwave", "--device-id", "mmwave_bridge", "--session-id", "bench_13"],
        input=result.stdout, capture_output=True, timeout=5)
    assert bridged.returncode == 0
    assert bridged.stderr == b""
    assert bridged.stdout == (b"LC1,mmwave_bridge,bench_13,1,presence,1\n"
                              b"LC1,mmwave_bridge,bench_13,2,presence,0\n")
