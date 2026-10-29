#!/usr/bin/env python3
"""Synthetic ESPHome text -> real presence JSON -> real LC1 bridge."""
import importlib.util
import io
import json
import subprocess
import sys
import unittest
from pathlib import Path

APP = Path(__file__).resolve().parents[2]
FILTER = APP / "tools/esphome_presence_filter.py"


def log(value="1.00000", name="Target number", component="sensor", suffix=""):
    return (f"[17:20:03][D][{component}:094]: '{name}': Sending state "
            f"{value} with 0 decimals of accuracy{suffix}\n").encode("ascii")


class ESPHomePresenceTests(unittest.TestCase):
    def load(self):
        self.assertTrue(FILTER.is_file(), "bounded text presence filter is missing")
        spec = importlib.util.spec_from_file_location("esphome_presence_filter", FILTER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def run_filter(self, data, args=()):
        self.load()
        return subprocess.run([sys.executable, str(FILTER), *args], input=data,
                              capture_output=True, timeout=5)

    def test_exact_sensor_target_count_is_boolean_only(self):
        result = self.run_filter(log("0.00000") + log() + log("255.00000"))
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, b"")
        self.assertEqual([json.loads(x) for x in result.stdout.splitlines()],
                         [{"detected": False}, {"detected": True}, {"detected": True}])

    def test_ansi_and_crlf_and_no_timestamp(self):
        sample = b"\x1b[0;36m" + log(name="Target Number").rstrip(b"\n") + b"\x1b[0m\r\n"
        sample += log("0").replace(b"[17:20:03]", b"")
        result = self.run_filter(sample)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b'{"detected":true}\n{"detected":false}\n')

    def test_actual_firmware_spacing_between_state_and_accuracy(self):
        # A bounded live structural probe matched flexible spacing but not single
        # spacing. No actual log/vital data is used as a saved fixture.
        sample = log("0.00000").replace(b" with ", b"  with ")
        result = self.run_filter(sample)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b'{"detected":false}\n')

    def test_only_target_number_never_vitals_or_similar_names(self):
        sample = b"".join(log(name=name) for name in (
            "Real-time heart rate", "Real-time respiratory rate",
            "Distance to detection object", "Illuminance", "Not target number",
            "Target number old", "Target number: private"))
        result = self.run_filter(sample)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, b"")
        self.assertEqual(result.stderr, b"no validated presence report\n")

    def test_bad_numbers_unknown_not_absence(self):
        for value in ("-1", "256", "1.5", "nan", "inf", "1e0", "1junk", "", "0000000001"):
            with self.subTest(value=value):
                result = self.run_filter(log(value))
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, b"")

    def test_wrong_source_or_appended_untrusted_text_rejected(self):
        for sample in (log(component="text_sensor"), log(component="api"),
                       log().replace(b"[D]", b"[C]"), log(suffix=" secret_suffix"),
                       b"untrusted " + log(), log() + b"truncated"):
            with self.subTest(sample=sample):
                result = self.run_filter(sample)
                expected = b'{"detected":true}\n' if sample == log() + b"truncated" else b""
                self.assertEqual(result.stdout, expected)

    def test_incomplete_final_line_is_not_a_report(self):
        result = self.run_filter(log().rstrip(b"\n"))
        self.assertEqual(result.returncode, 2)
        self.assertEqual(result.stdout, b"")

    def test_oversize_line_is_drained_not_parsed_as_suffix(self):
        result = self.run_filter(b"X" * 100000 + log() + log("0"))
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, b'{"detected":false}\n')

    def test_invalid_bytes_and_control_characters_are_not_stripped_into_validity(self):
        for sample in (b"\xff" + log(), b"\x00" + log(), b"\x1b[2J" + log()):
            with self.subTest(sample=sample):
                result = self.run_filter(sample)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, b"")

    def test_stream_reads_are_bounded_and_chunk_splits_work(self):
        module = self.load()
        class SplitInput:
            def __init__(self): self.data = bytearray(log() + log("0"))
            def read(self, count):
                self_outer.assertLessEqual(count, 128)
                if not self.data: return b""
                value = bytes(self.data[:1]); del self.data[:1]; return value
        self_outer = self
        out, err = io.StringIO(), io.StringIO()
        self.assertEqual(module.filter_stream(SplitInput(), out, err), 0)
        self.assertEqual(out.getvalue(), '{"detected":true}\n{"detected":false}\n')

    def test_no_arguments_can_open_hardware(self):
        result = self.run_filter(b"", ("/dev/cu.usbmodem21401",))
        self.assertEqual(result.returncode, 64)
        self.assertEqual(result.stdout, b"")
        self.assertNotIn(b"usbmodem", result.stderr)

    def test_fixed_read_error_does_not_leak_upstream_details(self):
        module = self.load()
        class BadInput:
            def read(self, count): raise OSError("private upstream readings")
        out, err = io.StringIO(), io.StringIO()
        self.assertEqual(module.filter_stream(BadInput(), out, err), 1)
        self.assertEqual(out.getvalue(), "")
        self.assertEqual(err.getvalue(), "presence input unavailable\n")

    def test_fixed_write_error_does_not_leak_upstream_details(self):
        module = self.load()
        class BadOutput:
            def write(self, value): raise BrokenPipeError("private device path")
        err = io.StringIO()
        self.assertEqual(module.filter_stream(io.BytesIO(log()), BadOutput(), err), 1)
        self.assertEqual(err.getvalue(), "presence output unavailable\n")

    def test_actual_filter_and_lc1_bridge(self):
        result = self.run_filter(log() + log("0"))
        self.assertEqual(result.returncode, 0)
        bridged = subprocess.run([sys.executable, str(APP / "tools/presence_bridge.py"),
            "--source", "mmwave", "--device-id", "mmwave_bridge", "--session-id", "bench_13"],
            input=result.stdout, capture_output=True, timeout=5)
        self.assertEqual(bridged.returncode, 0)
        self.assertEqual(bridged.stderr, b"")
        self.assertEqual(bridged.stdout,
            b"LC1,mmwave_bridge,bench_13,1,presence,1\nLC1,mmwave_bridge,bench_13,2,presence,0\n")


if __name__ == "__main__":
    unittest.main()
