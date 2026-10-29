import contextlib
import importlib.util
import io
import os
import pathlib
import shutil
import subprocess
import stat
import tempfile
import unittest
from unittest import mock


ROOT = pathlib.Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "generate_private_config.py"


def load_generator():
    spec = importlib.util.spec_from_file_location("lc_private_config", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class PrivateConfigTest(unittest.TestCase):
    def valid_environment(self):
        return {
            "LC_CAT_DEVICE_ID": "cat_a",
            "LC_WIFI_SSID": "synthetic-network",
            "LC_WIFI_PASSWORD": "synthetic-password",
            "LC_LOCAL_S1_HOST": "192.168.50.2",
            "LC_LOCAL_S1_PORT": "18765",
            "LC_LOCAL_PAIRING_KEY_HEX": "ab" * 32,
        }

    def test_writes_ignored_header_with_owner_only_permissions(self):
        module = load_generator()
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            module.OUTPUT = root / "living_canvas_private_config.h"
            stdout = io.StringIO()
            stderr = io.StringIO()
            environment = self.valid_environment()
            with mock.patch.dict(os.environ, environment, clear=True):
                with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                    self.assertEqual(module.main(), 0)

            generated = module.OUTPUT.read_text(encoding="utf-8")
            self.assertIn('#define LC_CAT_DEVICE_ID "cat_a"', generated)
            self.assertIn('#define LC_WIFI_SSID "synthetic-network"', generated)
            self.assertIn('#define LC_LOCAL_S1_HOST "192.168.50.2"', generated)
            self.assertIn('#define LC_LOCAL_S1_PORT 18765', generated)
            self.assertIn(
                '#define LC_LOCAL_PAIRING_KEY_HEX "' + "ab" * 32 + '"',
                generated,
            )
            self.assertNotIn(environment["LC_WIFI_PASSWORD"], stdout.getvalue())
            self.assertNotIn(
                environment["LC_LOCAL_PAIRING_KEY_HEX"], stdout.getvalue()
            )
            self.assertNotIn("LC_ACCESSORY_EVENTS_URL", generated)
            self.assertNotIn("LC_ACCESSORY_DEVICE_TOKEN", generated)
            self.assertNotIn("LC_BACKEND_CA_PEM", generated)
            self.assertEqual(stderr.getvalue(), "")
            self.assertEqual(
                stat.S_IMODE(module.OUTPUT.stat().st_mode),
                0o600,
            )

    def test_rejects_non_private_or_malformed_local_receiver_address(self):
        module = load_generator()
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            module.OUTPUT = root / "living_canvas_private_config.h"
            for host in (
                "api.example.invalid",
                "127.0.0.1",
                "0.0.0.0",
                "224.0.0.1",
                "8.8.8.8",
            ):
                with self.subTest(host=host):
                    module.OUTPUT.unlink(missing_ok=True)
                    environment = self.valid_environment()
                    environment["LC_LOCAL_S1_HOST"] = host
                    with mock.patch.dict(os.environ, environment, clear=True):
                        with contextlib.redirect_stderr(io.StringIO()):
                            self.assertEqual(module.main(), 2)
                    self.assertFalse(module.OUTPUT.exists())

    def test_rejects_invalid_local_port_or_pairing_key(self):
        module = load_generator()
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            module.OUTPUT = root / "living_canvas_private_config.h"
            for name, value in (
                ("LC_LOCAL_S1_PORT", "0"),
                ("LC_LOCAL_S1_PORT", "65536"),
                ("LC_LOCAL_S1_PORT", "not-a-port"),
                ("LC_LOCAL_PAIRING_KEY_HEX", "ab" * 31),
                ("LC_LOCAL_PAIRING_KEY_HEX", "AB" * 32),
                ("LC_LOCAL_PAIRING_KEY_HEX", "zz" * 32),
            ):
                with self.subTest(name=name, value=value):
                    module.OUTPUT.unlink(missing_ok=True)
                    environment = self.valid_environment()
                    environment[name] = value
                    with mock.patch.dict(os.environ, environment, clear=True):
                        with contextlib.redirect_stderr(io.StringIO()):
                            self.assertEqual(module.main(), 2)
                    self.assertFalse(module.OUTPUT.exists())

    def test_rejects_wifi_passwords_outside_printable_ascii_contract(self):
        module = load_generator()
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            module.OUTPUT = root / "living_canvas_private_config.h"
            for password in (
                "short7",
                "x" * 64,
                "valid-but-换成非ASCII",
                "line\nbreak",
                "tab\tinside",
            ):
                with self.subTest(password=repr(password)):
                    module.OUTPUT.unlink(missing_ok=True)
                    environment = self.valid_environment()
                    environment["LC_WIFI_PASSWORD"] = password
                    with mock.patch.dict(os.environ, environment, clear=True):
                        with contextlib.redirect_stderr(io.StringIO()):
                            self.assertEqual(module.main(), 2)
                    self.assertFalse(module.OUTPUT.exists())

    def test_honors_explicit_private_output_path(self):
        module = load_generator()
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            destination = root / "private" / "device-config.h"
            environment = self.valid_environment()
            environment["LC_PRIVATE_CONFIG_OUTPUT"] = str(destination)
            with mock.patch.dict(os.environ, environment, clear=True):
                with contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(module.main(), 0)

            self.assertTrue(destination.is_file())
            self.assertEqual(stat.S_IMODE(destination.stat().st_mode), 0o600)

    def test_atomic_replace_failure_does_not_leave_a_secret_staging_file(self):
        module = load_generator()
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            module.OUTPUT = root / "living_canvas_private_config.h"
            module.OUTPUT.write_text("existing-safe-config\n", encoding="utf-8")
            environment = self.valid_environment()

            with mock.patch.dict(os.environ, environment, clear=True):
                with mock.patch.object(
                    module.os,
                    "replace",
                    side_effect=OSError("synthetic replace failure"),
                ):
                    with contextlib.redirect_stderr(io.StringIO()):
                        self.assertEqual(module.main(), 2)

            self.assertEqual(
                module.OUTPUT.read_text(encoding="utf-8"),
                "existing-safe-config\n",
            )
            self.assertEqual(
                list(root.glob(".living_canvas_private_config.*")),
                [],
            )


class DeviceBuildTest(unittest.TestCase):
    def test_generic_build_does_not_embed_stale_private_config(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory) / "preview"
            (root / "tools").mkdir(parents=True)
            (root / "generated").mkdir()
            (root / "src").mkdir()
            (root / "data").mkdir()
            shutil.copy2(ROOT / "tools" / "build_firmware.sh",
                         root / "tools" / "build_firmware.sh")
            (root / "living_canvas_esp32s3_preview.ino").write_text(
                "void setup() {}\nvoid loop() {}\n", encoding="utf-8"
            )
            (root / "board_pins.h").write_text("#pragma once\n", encoding="utf-8")
            (root / "partitions.csv").write_text("# synthetic\n", encoding="utf-8")
            (root / "generated" / "living_canvas_asset_manifest.h").write_text(
                "#pragma once\n", encoding="utf-8"
            )
            stale = "STALE_PRIVATE_CONFIG_MUST_NOT_BE_STAGED"
            (root / "generated" / "living_canvas_private_config.h").write_text(
                stale, encoding="utf-8"
            )
            (root / "data" / "placeholder.bin").write_bytes(b"asset")

            fake_arduino = root / "fake-arduino.sh"
            fake_arduino.write_text(
                "#!/bin/sh\n"
                "set -eu\n"
                "build_path=\n"
                "output_dir=\n"
                "stage_dir=\n"
                "while [ \"$#\" -gt 0 ]; do\n"
                "  case \"$1\" in\n"
                "    --build-path) shift; build_path=$1 ;;\n"
                "    --output-dir) shift; output_dir=$1 ;;\n"
                "    *) stage_dir=$1 ;;\n"
                "  esac\n"
                "  shift\n"
                "done\n"
                "test ! -e \"$stage_dir/generated/living_canvas_private_config.h\"\n"
                "mkdir -p \"$build_path\" \"$output_dir\"\n"
                "app=living_canvas_esp32s3_preview.ino\n"
                "printf boot > \"$output_dir/$app.bootloader.bin\"\n"
                "printf part > \"$output_dir/$app.partitions.bin\"\n"
                "printf app > \"$output_dir/$app.bin\"\n"
                "printf ota > \"$build_path/boot_app0.bin\"\n",
                encoding="utf-8",
            )
            fake_arduino.chmod(0o700)
            build_root = root / "build-output"
            environment = {
                **os.environ,
                "ARDUINO_CLI": str(fake_arduino),
                "LC_BUILD_ROOT": str(build_root),
            }
            environment.pop("LC_PRIVATE_CONFIG_HEADER", None)

            result = subprocess.run(
                [str(root / "tools" / "build_firmware.sh")],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((build_root / "firmware" /
                             "living_canvas_esp32s3_preview.ino.bin").is_file())

    def test_builds_one_identity_without_logging_plaintext_secrets(self):
        wrapper = ROOT / "tools" / "build_device_firmware.sh"
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            fake_builder = root / "fake-builder.sh"
            fake_builder.write_text(
                "#!/bin/sh\n"
                "set -eu\n"
                "test -f \"$LC_PRIVATE_CONFIG_HEADER\"\n"
                "test \"$(stat -f %Lp \"$LC_PRIVATE_CONFIG_HEADER\")\" = 600\n"
                "mkdir -p \"$LC_BUILD_ROOT/firmware\" \"$LC_BUILD_ROOT/arduino\"\n"
                "printf app > \"$LC_BUILD_ROOT/firmware/living_canvas_esp32s3_preview.ino.bin\"\n"
                "printf boot > \"$LC_BUILD_ROOT/firmware/living_canvas_esp32s3_preview.ino.bootloader.bin\"\n"
                "printf part > \"$LC_BUILD_ROOT/firmware/living_canvas_esp32s3_preview.ino.partitions.bin\"\n"
                "printf ota > \"$LC_BUILD_ROOT/arduino/boot_app0.bin\"\n",
                encoding="utf-8",
            )
            fake_builder.chmod(0o700)
            delivery_root = root / "deliveries"
            environment = {
                **os.environ,
                "LC_CAT_DEVICE_ID": "cat_a",
                "LC_WIFI_SSID": "synthetic-network",
                "LC_WIFI_PASSWORD": "synthetic-password",
                "LC_LOCAL_S1_HOST": "192.168.50.2",
                "LC_LOCAL_S1_PORT": "18765",
                "LC_LOCAL_PAIRING_KEY_HEX": "ab" * 32,
                "LC_FIRMWARE_BUILDER": str(fake_builder),
                "LC_DEVICE_BUILD_ROOT": str(delivery_root),
            }
            result = subprocess.run(
                [str(wrapper)],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn(environment["LC_WIFI_PASSWORD"], result.stdout)
            self.assertNotIn(environment["LC_LOCAL_PAIRING_KEY_HEX"], result.stdout)
            deliveries = list((delivery_root / "cat_a").iterdir())
            self.assertEqual(len(deliveries), 1)
            delivery = deliveries[0]
            self.assertTrue((delivery / "checksums.sha256").is_file())
            self.assertTrue((delivery / "identity.txt").is_file())
            self.assertEqual(
                (delivery / "identity.txt").read_text(encoding="utf-8"),
                "cat_a\n",
            )
            delivered_text = "\n".join(
                path.read_text(encoding="utf-8", errors="ignore")
                for path in delivery.iterdir()
                if path.is_file()
            )
            self.assertNotIn(environment["LC_WIFI_PASSWORD"], delivered_text)
            self.assertNotIn(environment["LC_LOCAL_PAIRING_KEY_HEX"], delivered_text)

    def test_builds_from_a_matched_pregenerated_pairing_header(self):
        wrapper = ROOT / "tools" / "build_device_firmware.sh"
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            private_header = root / "cat_a-private.h"
            private_header.write_text(
                '#pragma once\n\n#define LC_CAT_DEVICE_ID "cat_a"\n'
                '#define LC_WIFI_SSID "synthetic-network"\n'
                '#define LC_WIFI_PASSWORD "synthetic-password"\n'
                '#define LC_LOCAL_S1_HOST "192.168.50.2"\n'
                '#define LC_LOCAL_S1_PORT 18765\n'
                '#define LC_LOCAL_PAIRING_KEY_HEX "' + "1a" * 32 + '"\n',
                encoding="utf-8",
            )
            private_header.chmod(0o600)
            fake_builder = root / "fake-builder.sh"
            fake_builder.write_text(
                "#!/bin/sh\n"
                "set -eu\n"
                "grep -Fxq '#define LC_CAT_DEVICE_ID \"cat_a\"' \"$LC_PRIVATE_CONFIG_HEADER\"\n"
                "mkdir -p \"$LC_BUILD_ROOT/firmware\" \"$LC_BUILD_ROOT/arduino\"\n"
                "printf app > \"$LC_BUILD_ROOT/firmware/living_canvas_esp32s3_preview.ino.bin\"\n"
                "printf boot > \"$LC_BUILD_ROOT/firmware/living_canvas_esp32s3_preview.ino.bootloader.bin\"\n"
                "printf part > \"$LC_BUILD_ROOT/firmware/living_canvas_esp32s3_preview.ino.partitions.bin\"\n"
                "printf ota > \"$LC_BUILD_ROOT/arduino/boot_app0.bin\"\n",
                encoding="utf-8",
            )
            fake_builder.chmod(0o700)
            delivery_root = root / "deliveries"
            environment = {
                **os.environ,
                "LC_CAT_DEVICE_ID": "cat_a",
                "LC_PRIVATE_CONFIG_HEADER": str(private_header),
                "LC_FIRMWARE_BUILDER": str(fake_builder),
                "LC_DEVICE_BUILD_ROOT": str(delivery_root),
            }

            result = subprocess.run(
                [str(wrapper)], check=False, capture_output=True, text=True,
                env=environment,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            delivery = next((delivery_root / "cat_a").iterdir())
            self.assertEqual((delivery / "identity.txt").read_text(), "cat_a\n")
            self.assertFalse(any("PRIVATE_CONFIG" in path.name.upper()
                                 for path in delivery.iterdir()))

    def test_rejects_a_pregenerated_header_for_the_other_cat(self):
        wrapper = ROOT / "tools" / "build_device_firmware.sh"
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            private_header = root / "wrong-cat.h"
            private_header.write_text(
                '#pragma once\n#define LC_CAT_DEVICE_ID "cat_b"\n', encoding="utf-8"
            )
            private_header.chmod(0o600)
            marker = root / "builder-ran"
            fake_builder = root / "fake-builder.sh"
            fake_builder.write_text(
                "#!/bin/sh\nprintf ran > \"$LC_TEST_BUILDER_MARKER\"\n",
                encoding="utf-8",
            )
            fake_builder.chmod(0o700)
            environment = {
                **os.environ,
                "LC_CAT_DEVICE_ID": "cat_a",
                "LC_PRIVATE_CONFIG_HEADER": str(private_header),
                "LC_FIRMWARE_BUILDER": str(fake_builder),
                "LC_TEST_BUILDER_MARKER": str(marker),
                "LC_DEVICE_BUILD_ROOT": str(root / "deliveries"),
            }

            result = subprocess.run(
                [str(wrapper)], check=False, capture_output=True, text=True,
                env=environment,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(marker.exists())

    def test_failed_device_delivery_removes_secret_bearing_incoming_directory(self):
        wrapper = ROOT / "tools" / "build_device_firmware.sh"
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            fake_builder = root / "fake-builder.sh"
            fake_builder.write_text(
                "#!/bin/sh\n"
                "set -eu\n"
                "mkdir -p \"$LC_BUILD_ROOT/firmware\" \"$LC_BUILD_ROOT/arduino\"\n"
                "printf app > \"$LC_BUILD_ROOT/firmware/living_canvas_esp32s3_preview.ino.bin\"\n"
                "printf boot > \"$LC_BUILD_ROOT/firmware/living_canvas_esp32s3_preview.ino.bootloader.bin\"\n"
                "printf part > \"$LC_BUILD_ROOT/firmware/living_canvas_esp32s3_preview.ino.partitions.bin\"\n"
                "printf ota > \"$LC_BUILD_ROOT/arduino/boot_app0.bin\"\n",
                encoding="utf-8",
            )
            fake_builder.chmod(0o700)
            fake_bin = root / "bin"
            fake_bin.mkdir()
            fake_shasum = fake_bin / "shasum"
            fake_shasum.write_text("#!/bin/sh\nexit 23\n", encoding="utf-8")
            fake_shasum.chmod(0o700)
            delivery_root = root / "deliveries"
            environment = {
                **os.environ,
                "PATH": str(fake_bin) + os.pathsep + os.environ["PATH"],
                "LC_CAT_DEVICE_ID": "cat_a",
                "LC_WIFI_SSID": "synthetic-network",
                "LC_WIFI_PASSWORD": "synthetic-password",
                "LC_LOCAL_S1_HOST": "192.168.50.2",
                "LC_LOCAL_S1_PORT": "18765",
                "LC_LOCAL_PAIRING_KEY_HEX": "ab" * 32,
                "LC_FIRMWARE_BUILDER": str(fake_builder),
                "LC_DEVICE_BUILD_ROOT": str(delivery_root),
            }

            result = subprocess.run(
                [str(wrapper)],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )

            self.assertNotEqual(result.returncode, 0)
            device_root = delivery_root / "cat_a"
            self.assertEqual(
                list(device_root.glob(".incoming-*")) if device_root.exists() else [],
                [],
            )


if __name__ == "__main__":
    unittest.main()
