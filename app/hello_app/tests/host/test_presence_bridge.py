#!/usr/bin/env python3

import importlib.util
import unittest
from pathlib import Path


APP_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = APP_ROOT / "tools" / "presence_bridge.py"
SPEC = importlib.util.spec_from_file_location("presence_bridge", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
presence_bridge = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(presence_bridge)


class PresenceBridgeTests(unittest.TestCase):
    def test_mmwave_bridge_emits_bounded_binary_presence_events(self):
        bridge = presence_bridge.PresenceBridge(
            source="mmwave", device_id="mmwave_bridge", session_id="bench_01"
        )
        self.assertEqual(
            bridge.encode({"detected": True}),
            "LC1,mmwave_bridge,bench_01,1,presence,1\n",
        )
        self.assertEqual(
            bridge.encode({"detected": False}),
            "LC1,mmwave_bridge,bench_01,2,presence,0\n",
        )

    def test_camera_bridge_emits_confidence_without_image_data(self):
        bridge = presence_bridge.PresenceBridge(
            source="camera", device_id="camera_bridge", session_id="bench_02"
        )
        self.assertEqual(
            bridge.encode({"detected": True, "confidence": 82}),
            "LC1,camera_bridge,bench_02,1,presence_confidence,82\n",
        )
        self.assertEqual(
            bridge.encode({"detected": False, "confidence": 92}),
            "LC1,camera_bridge,bench_02,2,presence_confidence,0\n",
        )

    def test_privacy_sensitive_fields_are_rejected(self):
        for private_key in ("image", "frame", "face", "identity", "embedding"):
            with self.subTest(private_key=private_key):
                bridge = presence_bridge.PresenceBridge(
                    source="camera",
                    device_id="camera_bridge",
                    session_id="bench_03",
                )
                with self.assertRaisesRegex(ValueError, "privacy-sensitive"):
                    bridge.encode(
                        {"detected": True, "confidence": 80, private_key: "data"}
                    )

    def test_invalid_camera_confidence_fails_closed(self):
        for confidence in (-1, 101, True, "82"):
            with self.subTest(confidence=confidence):
                bridge = presence_bridge.PresenceBridge(
                    source="camera",
                    device_id="camera_bridge",
                    session_id="bench_04",
                )
                with self.assertRaisesRegex(ValueError, "confidence"):
                    bridge.encode({"detected": True, "confidence": confidence})

    def test_identifiers_are_restricted_to_wire_safe_characters(self):
        with self.assertRaisesRegex(ValueError, "device_id"):
            presence_bridge.PresenceBridge(
                source="mmwave", device_id="bad,device", session_id="bench_05"
            )


if __name__ == "__main__":
    unittest.main()
