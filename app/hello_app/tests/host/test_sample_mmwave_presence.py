#!/usr/bin/env python3
"""Safety and data-minimisation tests for bounded MR60 sampling."""

import importlib.util
import io
import os
import subprocess
import sys
import unittest
from pathlib import Path


APP = Path(__file__).resolve().parents[2]
TOOLS = APP / "tools"
SAMPLER = TOOLS / "sample_mmwave_presence.py"
CONFIRMATION = "人在板旁、供电稳定、线缆不动"
TEST_MMWAVE_SERIAL = "02:00:00:00:00:01"


def report(value=b"1.00000"):
    return (b"[17:20:03][D][sensor:094]: 'Target number': Sending state "
            + value + b" with 0 decimals of accuracy\n")


class SampleMmwavePresenceTests(unittest.TestCase):
    def load(self):
        self.assertTrue(SAMPLER.is_file(), "bounded mmWave sampler is missing")
        sys.path.insert(0, str(TOOLS))
        try:
            spec = importlib.util.spec_from_file_location("sample_mmwave_presence", SAMPLER)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            return module
        finally:
            sys.path.pop(0)

    def test_default_is_hardware_free_dry_run(self):
        result = subprocess.run(
            [sys.executable, str(SAMPLER)], capture_output=True, text=True, timeout=5
        )
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, "")
        self.assertIn("dry run", result.stdout.lower())

    def test_requires_exact_fresh_operator_confirmation(self):
        module = self.load()
        for confirmation in ("", "人在板旁", CONFIRMATION + "。"):
            with self.subTest(confirmation=confirmation):
                with self.assertRaises(ValueError):
                    module.sample_presence(
                        [], confirmation, io.StringIO(), io.StringIO(),
                        expected_serial=TEST_MMWAVE_SERIAL,
                    )

    def test_verified_port_is_opened_read_only_and_only_boolean_leaves_filter(self):
        module = self.load()
        port = type("Port", (), {
            "device": "/dev/cu.usbmodem21401",
            "serial_number": TEST_MMWAVE_SERIAL,
            "vid": module.EXPECTED_VID,
        })()
        chunks = [report(), b""]
        opened = []
        closed = []

        def opener(path, flags):
            opened.append((path, flags))
            return 17

        out, err = io.StringIO(), io.StringIO()
        result = module.sample_presence(
            [port], CONFIRMATION, out, err,
            expected_serial=TEST_MMWAVE_SERIAL,
            opener=opener,
            closer=closed.append,
            reader=lambda _fd, _count: chunks.pop(0),
            waiter=lambda *_args: ([17], [], []),
            clock=lambda: 0.0,
        )

        self.assertEqual(result, 0)
        self.assertEqual(out.getvalue(), '{"detected":true}\n')
        self.assertEqual(err.getvalue(), "")
        self.assertEqual(closed, [17])
        self.assertEqual(opened[0][0], "/dev/cu.usbmodem21401")
        flags = opened[0][1]
        self.assertEqual(flags & os.O_ACCMODE, os.O_RDONLY)
        self.assertTrue(flags & os.O_NOCTTY)
        self.assertTrue(flags & os.O_NONBLOCK)

    def test_source_has_no_serial_configuration_or_write_path(self):
        source = SAMPLER.read_text(encoding="utf-8")
        for forbidden in ("serial.Serial", "O_RDWR", "O_WRONLY", "tcsetattr", ".write("):
            self.assertNotIn(forbidden, source)


if __name__ == "__main__":
    unittest.main()
