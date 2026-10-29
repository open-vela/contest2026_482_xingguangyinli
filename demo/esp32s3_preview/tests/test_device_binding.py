#!/usr/bin/env python3
"""Stable physical-board to check-in identity bindings."""

import importlib.util
import json
import pathlib
import stat
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "tools" / "device_binding.py"


class DeviceBindingTests(unittest.TestCase):
    def load(self):
        self.assertTrue(MODULE_PATH.is_file(), "device binding tool is missing")
        spec = importlib.util.spec_from_file_location("device_binding", MODULE_PATH)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_first_binding_is_atomic_private_and_repeatable(self):
        module = self.load()
        with tempfile.TemporaryDirectory() as directory:
            registry = pathlib.Path(directory) / "bindings.json"
            module.check_binding(
                registry, "cat_a", "CAT-A-USB-001", "AA:BB:CC:DD:EE:01"
            )
            module.record_binding(
                registry, "cat_a", "CAT-A-USB-001", "AA:BB:CC:DD:EE:01"
            )
            module.check_binding(
                registry, "cat_a", "CAT-A-USB-001", "aa:bb:cc:dd:ee:01"
            )
            module.record_binding(
                registry, "cat_a", "CAT-A-USB-001", "aa:bb:cc:dd:ee:01"
            )

            payload = json.loads(registry.read_text(encoding="utf-8"))
            self.assertEqual(payload["version"], 1)
            self.assertEqual(payload["bindings"], {
                "cat_a": {
                    "usb_serial": "CAT-A-USB-001",
                    "mac": "aa:bb:cc:dd:ee:01",
                }
            })
            self.assertEqual(stat.S_IMODE(registry.stat().st_mode), 0o600)
            self.assertEqual(list(registry.parent.glob(".bindings.*")), [])

    def test_two_distinct_boards_can_bind_to_distinct_slots(self):
        module = self.load()
        with tempfile.TemporaryDirectory() as directory:
            registry = pathlib.Path(directory) / "bindings.json"
            module.record_binding(
                registry, "cat_a", "CAT-A-USB-001", "AA:BB:CC:DD:EE:01"
            )
            module.record_binding(
                registry, "cat_b", "CAT-B-USB-002", "AA:BB:CC:DD:EE:02"
            )
            module.check_binding(
                registry, "cat_a", "CAT-A-USB-001", "AA:BB:CC:DD:EE:01"
            )
            module.check_binding(
                registry, "cat_b", "CAT-B-USB-002", "AA:BB:CC:DD:EE:02"
            )

    def test_rejects_cross_binding_and_rebinding(self):
        module = self.load()
        with tempfile.TemporaryDirectory() as directory:
            registry = pathlib.Path(directory) / "bindings.json"
            module.record_binding(
                registry, "cat_a", "CAT-A-USB-001", "AA:BB:CC:DD:EE:01"
            )
            rejected = (
                ("cat_b", "CAT-A-USB-001", "AA:BB:CC:DD:EE:01"),
                ("cat_a", "CAT-B-USB-002", "AA:BB:CC:DD:EE:02"),
                ("cat_b", "CAT-B-USB-002", "AA:BB:CC:DD:EE:01"),
                ("cat_b", "CAT-A-USB-001", "AA:BB:CC:DD:EE:02"),
            )
            for binding in rejected:
                with self.subTest(binding=binding):
                    with self.assertRaises(ValueError):
                        module.check_binding(registry, *binding)
                    with self.assertRaises(ValueError):
                        module.record_binding(registry, *binding)

    def test_rejects_corrupt_unknown_or_unsafe_registry(self):
        module = self.load()
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            registry = root / "bindings.json"
            invalid_payloads = (
                "not json",
                '{"version":2,"bindings":{}}',
                '{"version":1,"bindings":{"cat_c":{}}}',
                '{"version":1,"bindings":{"cat_a":{"usb_serial":"x"}}}',
            )
            for payload in invalid_payloads:
                registry.write_text(payload, encoding="utf-8")
                registry.chmod(0o600)
                with self.subTest(payload=payload):
                    with self.assertRaises(ValueError):
                        module.check_binding(
                            registry, "cat_a", "CAT-A", "AA:BB:CC:DD:EE:01"
                        )

            registry.write_text(
                '{"version":1,"bindings":{}}\n', encoding="utf-8"
            )
            registry.chmod(0o644)
            with self.assertRaises(ValueError):
                module.check_binding(
                    registry, "cat_a", "CAT-A", "AA:BB:CC:DD:EE:01"
                )

            real_registry = root / "real-bindings.json"
            real_registry.write_text(
                '{"version":1,"bindings":{}}\n', encoding="utf-8"
            )
            real_registry.chmod(0o600)
            registry.unlink()
            registry.symlink_to(real_registry)
            with self.assertRaises(ValueError):
                module.check_binding(
                    registry, "cat_a", "CAT-A", "AA:BB:CC:DD:EE:01"
                )

            with self.assertRaises(ValueError):
                module.check_binding(
                    root / "missing" / "bindings.json",
                    "cat_a", "CAT-A", "AA:BB:CC:DD:EE:01",
                )
            with self.assertRaises(ValueError):
                module.check_binding(
                    root / "bindings.json", "cat_c", "CAT-C",
                    "AA:BB:CC:DD:EE:03",
                )


if __name__ == "__main__":
    unittest.main()
