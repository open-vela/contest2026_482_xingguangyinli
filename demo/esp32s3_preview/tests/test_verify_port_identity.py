#!/usr/bin/env python3
"""USB metadata gate before any esptool access to a cat board."""

import importlib.util
import subprocess
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[1]
VERIFIER = ROOT / "tools" / "verify_port_identity.py"
TEST_RADAR_SERIAL = "02:00:00:00:00:01"
TEST_CAMERA_SERIAL = "02:00:00:00:00:02"
TEST_RESERVED_SERIALS = {TEST_RADAR_SERIAL, TEST_CAMERA_SERIAL}


class VerifyPortIdentityTests(unittest.TestCase):
    def load(self):
        self.assertTrue(VERIFIER.is_file(), "ESP32 USB identity verifier is missing")
        spec = importlib.util.spec_from_file_location("verify_port_identity", VERIFIER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    @staticmethod
    def port(device, serial_number, vid=0x303A):
        return SimpleNamespace(device=device, serial_number=serial_number, vid=vid)

    def test_accepts_one_exact_cat_usb_identity(self):
        module = self.load()
        ports = [
            self.port("/dev/cu.usbmodem-cat-a", "CAT-A-USB-001"),
            self.port("/dev/cu.usbmodem-radar", TEST_RADAR_SERIAL),
        ]
        self.assertEqual(
            module.verify_port_identity(
                ports, "/dev/cu.usbmodem-cat-a", "CAT-A-USB-001",
                TEST_RESERVED_SERIALS,
            ),
            "/dev/cu.usbmodem-cat-a",
        )

    def test_reserved_non_cat_identities_are_private_and_required(self):
        module = self.load()
        configured = module.configured_non_cat_serials({
            "LIVING_CANVAS_NON_CAT_USB_SERIALS":
                TEST_RADAR_SERIAL + "," + TEST_CAMERA_SERIAL,
        })
        self.assertEqual(configured, TEST_RESERVED_SERIALS)
        for environment in ({}, {"LIVING_CANVAS_NON_CAT_USB_SERIALS": "bad value"}):
            with self.subTest(environment=environment):
                with self.assertRaises(ValueError):
                    module.configured_non_cat_serials(environment)

    def test_rejects_mmwave_camera_mismatch_duplicate_and_unsafe_path(self):
        module = self.load()
        cases = [
            ([self.port("/dev/cu.usbmodem-radar", TEST_RADAR_SERIAL)],
             "/dev/cu.usbmodem-radar", TEST_RADAR_SERIAL),
            ([self.port("/dev/cu.usbmodem-camera", TEST_CAMERA_SERIAL)],
             "/dev/cu.usbmodem-camera", TEST_CAMERA_SERIAL),
            ([self.port("/dev/cu.usbmodem-cat", "CAT-A")],
             "/dev/cu.usbmodem-cat", "CAT-B"),
            ([self.port("/dev/cu.usbmodem-cat", "CAT-A"),
              self.port("/dev/cu.usbmodem-cat", "CAT-A")],
             "/dev/cu.usbmodem-cat", "CAT-A"),
            ([self.port("/tmp/fake", "CAT-A")], "/tmp/fake", "CAT-A"),
            ([self.port("/dev/cu.usbmodem-cat", "CAT-A", 0x1234)],
             "/dev/cu.usbmodem-cat", "CAT-A"),
        ]
        for ports, device, serial_number in cases:
            with self.subTest(device=device, serial_number=serial_number):
                with self.assertRaises(ValueError):
                    module.verify_port_identity(
                        ports, device, serial_number, TEST_RESERVED_SERIALS
                    )

        cat = [self.port("/dev/cu.usbmodem-cat", "CAT-A")]
        for reserved in ((), {"contains space"}):
            with self.subTest(reserved=reserved):
                with self.assertRaises(ValueError):
                    module.verify_port_identity(
                        cat, "/dev/cu.usbmodem-cat", "CAT-A", reserved
                    )

    def test_cli_rejects_missing_identity_without_enumerating(self):
        result = subprocess.run(
            [sys.executable, str(VERIFIER)],
            capture_output=True,
            text=True,
            timeout=5,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("usbmodem", result.stderr)

    def test_source_only_enumerates_metadata(self):
        source = VERIFIER.read_text(encoding="utf-8")
        self.assertIn("list_ports.comports", source)
        self.assertIn("LIVING_CANVAS_NON_CAT_USB_SERIALS", source)
        self.assertNotIn("KNOWN_NON_CAT_SERIALS", source)
        for forbidden in ("serial.Serial", "esptool", "O_RDWR", "write_flash"):
            self.assertNotIn(forbidden, source)


if __name__ == "__main__":
    unittest.main()
