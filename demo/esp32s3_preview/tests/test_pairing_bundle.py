import contextlib
import importlib.util
import io
import os
import pathlib
import stat
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "generate_pairing_bundle.py"


def load_generator():
    spec = importlib.util.spec_from_file_location("lc_pairing_bundle", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class PairingBundleTest(unittest.TestCase):
    def environment(self):
        return {
            "LC_WIFI_SSID": "synthetic-network",
            "LC_WIFI_PASSWORD": "synthetic-password",
            "LC_LOCAL_S1_HOST": "192.168.50.2",
            "LC_LOCAL_S1_PORT": "18765",
            "LC_CAT_A_PAIRING_KEY_HEX": "1a" * 32,
            "LC_CAT_B_PAIRING_KEY_HEX": "2b" * 32,
        }

    def test_generates_atomic_private_three_party_bundle(self):
        module = load_generator()
        with tempfile.TemporaryDirectory() as directory:
            module.BUNDLE_ROOT = pathlib.Path(directory) / "pairing"
            stdout = io.StringIO()
            environment = self.environment()
            with mock.patch.dict(os.environ, environment, clear=True):
                with contextlib.redirect_stdout(stdout):
                    self.assertEqual(module.main(), 0)

            bundles = list(module.BUNDLE_ROOT.glob("pair-*"))
            self.assertEqual(len(bundles), 1)
            bundle = bundles[0]
            self.assertFalse(any(module.BUNDLE_ROOT.glob(".incoming-*")))
            self.assertEqual(stat.S_IMODE(bundle.stat().st_mode), 0o700)

            s1_keys = bundle / "s1" / "local_accessory_keys"
            cat_a = bundle / "cat_a" / "living_canvas_private_config.h"
            cat_b = bundle / "cat_b" / "living_canvas_private_config.h"
            for private_file in (s1_keys, cat_a, cat_b):
                self.assertTrue(private_file.is_file())
                self.assertEqual(stat.S_IMODE(private_file.stat().st_mode), 0o600)

            self.assertEqual(
                s1_keys.read_text(encoding="ascii"),
                "cat_a=" + environment["LC_CAT_A_PAIRING_KEY_HEX"] + "\n"
                "cat_b=" + environment["LC_CAT_B_PAIRING_KEY_HEX"] + "\n",
            )
            cat_a_text = cat_a.read_text(encoding="utf-8")
            cat_b_text = cat_b.read_text(encoding="utf-8")
            self.assertIn('#define LC_CAT_DEVICE_ID "cat_a"', cat_a_text)
            self.assertIn('#define LC_CAT_DEVICE_ID "cat_b"', cat_b_text)
            self.assertIn(environment["LC_CAT_A_PAIRING_KEY_HEX"], cat_a_text)
            self.assertIn(environment["LC_CAT_B_PAIRING_KEY_HEX"], cat_b_text)
            self.assertNotIn("LC_ACCESSORY_DEVICE_TOKEN", cat_a_text + cat_b_text)
            self.assertNotIn(environment["LC_WIFI_PASSWORD"], stdout.getvalue())
            self.assertNotIn(environment["LC_CAT_A_PAIRING_KEY_HEX"], stdout.getvalue())
            self.assertNotIn(environment["LC_CAT_B_PAIRING_KEY_HEX"], stdout.getvalue())

    def test_generates_independent_keys_when_neither_key_is_supplied(self):
        module = load_generator()
        with tempfile.TemporaryDirectory() as directory:
            module.BUNDLE_ROOT = pathlib.Path(directory) / "pairing"
            environment = self.environment()
            environment.pop("LC_CAT_A_PAIRING_KEY_HEX")
            environment.pop("LC_CAT_B_PAIRING_KEY_HEX")
            with mock.patch.dict(os.environ, environment, clear=True):
                with mock.patch.object(
                    module.secrets, "token_hex", side_effect=["3c" * 32, "4d" * 32]
                ):
                    with contextlib.redirect_stdout(io.StringIO()):
                        self.assertEqual(module.main(), 0)

            bundle = next(module.BUNDLE_ROOT.glob("pair-*"))
            document = (bundle / "s1" / "local_accessory_keys").read_text(
                encoding="ascii"
            )
            self.assertEqual(document, "cat_a=" + "3c" * 32 + "\ncat_b=" + "4d" * 32 + "\n")

    def test_rejects_partial_or_reused_keys_without_leaving_material(self):
        module = load_generator()
        with tempfile.TemporaryDirectory() as directory:
            module.BUNDLE_ROOT = pathlib.Path(directory) / "pairing"
            for mutation in ("missing_b", "same"):
                with self.subTest(mutation=mutation):
                    environment = self.environment()
                    if mutation == "missing_b":
                        environment.pop("LC_CAT_B_PAIRING_KEY_HEX")
                    else:
                        environment["LC_CAT_B_PAIRING_KEY_HEX"] = environment[
                            "LC_CAT_A_PAIRING_KEY_HEX"
                        ]
                    with mock.patch.dict(os.environ, environment, clear=True):
                        with contextlib.redirect_stderr(io.StringIO()):
                            self.assertEqual(module.main(), 2)
                    self.assertFalse(module.BUNDLE_ROOT.exists())

    def test_generator_rejection_removes_incoming_directory(self):
        module = load_generator()
        with tempfile.TemporaryDirectory() as directory:
            module.BUNDLE_ROOT = pathlib.Path(directory) / "pairing"
            environment = self.environment()
            environment["LC_WIFI_PASSWORD"] = "short"
            with mock.patch.dict(os.environ, environment, clear=True):
                with contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(module.main(), 2)
            self.assertEqual(
                list(module.BUNDLE_ROOT.glob(".incoming-*"))
                if module.BUNDLE_ROOT.exists()
                else [],
                [],
            )
            self.assertFalse(any(module.BUNDLE_ROOT.glob("pair-*")))


if __name__ == "__main__":
    unittest.main()
