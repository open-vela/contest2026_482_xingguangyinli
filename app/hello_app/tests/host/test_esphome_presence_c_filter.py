#!/usr/bin/env python3
"""Keep the native S1 and host ESPHome presence filters equivalent."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


APP = Path(__file__).resolve().parents[2]
PYTHON_FILTER = APP / "tools/esphome_presence_filter.py"
C_FILTER = APP / "tools/esphome_presence_filter.c"
C_PARSER = APP / "src/lc_esphome_presence.c"
INCLUDE = APP / "include"


def log(value="1.00000", name="Target number", component="sensor", suffix=""):
    return (f"[17:20:03][D][{component}:094]: '{name}': Sending state "
            f"{value} with 0 decimals of accuracy{suffix}\n").encode("ascii")


class NativeFilterParityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tempdir = tempfile.TemporaryDirectory(prefix="lc-presence-parity-")
        cls.native_filter = Path(cls.tempdir.name) / "esphome_presence_filter"
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2",
            f"-I{INCLUDE}", str(C_FILTER), str(C_PARSER), "-o", str(cls.native_filter),
        ], check=True, capture_output=True, timeout=15)

    @classmethod
    def tearDownClass(cls):
        cls.tempdir.cleanup()

    def assert_filters_match(self, sample):
        python_result = subprocess.run(
            [sys.executable, str(PYTHON_FILTER)], input=sample,
            capture_output=True, timeout=5)
        native_result = subprocess.run(
            [str(self.native_filter)], input=sample,
            capture_output=True, timeout=5)
        self.assertEqual(native_result.returncode, python_result.returncode)
        self.assertEqual(native_result.stdout, python_result.stdout)
        self.assertEqual(native_result.stderr, python_result.stderr)

    def test_valid_and_invalid_corpus_matches_host_contract(self):
        samples = [
            log("0.00000") + log() + log("255.00000"),
            b"\x1b[0;36m" + log(name="Target Number").rstrip(b"\n") + b"\x1b[0m\r\n",
            log("0").replace(b"[17:20:03]", b""),
            log().replace(b"[D][sensor", b"[d][SENSOR")
                 .replace(b"Sending state", b"sending STATE")
                 .replace(b" with ", b" WITH ")
                 .replace(b"decimals of accuracy", b"DECIMALS OF ACCURACY"),
            log("0.00000").replace(b" with ", b"  with "),
            log(name="Real-time heart rate") + log(name="Target number old"),
            log("-1") + log("256") + log("1.5") + log("nan"),
            log(component="text_sensor") + log().replace(b"[D]", b"[C]"),
            log(suffix=" secret_suffix"),
            log().rstrip(b"\n"),
            b"X" * 100000 + log() + log("0"),
            b"\xff" + log() + b"\x00" + log() + b"\x1b[2J" + log(),
        ]
        for sample in samples:
            with self.subTest(sample=sample[-128:]):
                self.assert_filters_match(sample)


if __name__ == "__main__":
    unittest.main()
