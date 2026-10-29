#!/usr/bin/env python3
"""Fail-closed USB identity resolution for the MR60 host relay."""

import importlib.util
import subprocess
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace


APP = Path(__file__).resolve().parents[2]
RESOLVER = APP / "tools/resolve_mmwave_port.py"
TEST_MMWAVE_SERIAL = "02:00:00:00:00:01"


class ResolveMmwavePortTests(unittest.TestCase):
    def load(self):
        self.assertTrue(RESOLVER.is_file(), "mmWave port resolver is missing")
        spec = importlib.util.spec_from_file_location("resolve_mmwave_port", RESOLVER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    @staticmethod
    def port(device, serial_number, vid=0x303A):
        return SimpleNamespace(device=device, serial_number=serial_number, vid=vid)

    def test_selects_one_exact_cu_port_by_fixed_usb_identity(self):
        module = self.load()
        ports = [
            self.port("/dev/cu.usbmodem21401", TEST_MMWAVE_SERIAL),
            self.port("/dev/cu.usbmodem99999", "some-other-board"),
            self.port("/dev/tty.usbmodem21401", TEST_MMWAVE_SERIAL),
        ]
        self.assertEqual(
            module.resolve_mmwave_port(ports, TEST_MMWAVE_SERIAL),
            "/dev/cu.usbmodem21401",
        )

    def test_private_identity_must_be_explicit_and_well_formed(self):
        module = self.load()
        self.assertEqual(
            module.configured_mmwave_serial({
                "LIVING_CANVAS_MMWAVE_USB_SERIAL": TEST_MMWAVE_SERIAL,
            }),
            TEST_MMWAVE_SERIAL,
        )
        for environment in ({}, {"LIVING_CANVAS_MMWAVE_USB_SERIAL": "bad value"}):
            with self.subTest(environment=environment):
                with self.assertRaises(ValueError):
                    module.configured_mmwave_serial(environment)

    def test_rejects_missing_duplicate_wrong_vendor_or_unsafe_path(self):
        module = self.load()
        exact = self.port("/dev/cu.usbmodem21401", TEST_MMWAVE_SERIAL)
        cases = [
            [],
            [exact, self.port("/dev/cu.usbmodem31401", TEST_MMWAVE_SERIAL)],
            [self.port("/dev/cu.usbmodem21401", TEST_MMWAVE_SERIAL, 0x1234)],
            [self.port("/tmp/fake-port", TEST_MMWAVE_SERIAL)],
            [self.port("/dev/cu.usbmodem21401", None)],
        ]
        for ports in cases:
            with self.subTest(ports=ports):
                with self.assertRaises(ValueError):
                    module.resolve_mmwave_port(ports, TEST_MMWAVE_SERIAL)

        for serial in ("", "contains space", "../unsafe", None):
            with self.subTest(serial=serial):
                with self.assertRaises(ValueError):
                    module.resolve_mmwave_port([exact], serial)

    def test_cli_rejects_arguments_without_echoing_them(self):
        result = subprocess.run(
            [sys.executable, str(RESOLVER), "/dev/cu.private"],
            capture_output=True,
            text=True,
            timeout=5,
        )
        self.assertEqual(result.returncode, 64)
        self.assertNotIn("private", result.stderr)

    def test_source_only_enumerates_and_never_opens_or_writes_a_port(self):
        source = RESOLVER.read_text(encoding="utf-8")
        self.assertIn("list_ports.comports", source)
        self.assertIn("LIVING_CANVAS_MMWAVE_USB_SERIAL", source)
        self.assertNotIn("EXPECTED_SERIAL", source)
        self.assertNotIn("serial.Serial", source)
        self.assertNotIn(".write(", source)


if __name__ == "__main__":
    unittest.main()
