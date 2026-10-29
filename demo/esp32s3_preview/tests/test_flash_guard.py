import os
import pathlib
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
FLASH = ROOT / "tools" / "flash_verified_device.sh"
PINNED_ESPTOOL = (
    ROOT.parents[2] / "toolchains" / "esptool-venv" / "bin" / "esptool.py"
)


class FlashGuardTest(unittest.TestCase):
    def test_uses_esptool_412_flash_option_names(self):
        flash_source = FLASH.read_text(encoding="utf-8")
        for option in ("--flash_mode", "--flash_freq", "--flash_size"):
            self.assertIn(option, flash_source)
        for obsolete in ("--flash-mode", "--flash-freq", "--flash-size"):
            self.assertNotIn(obsolete, flash_source)

    def test_pinned_esptool_supports_every_command_used_by_flash_guard(self):
        if not PINNED_ESPTOOL.is_file():
            self.skipTest("pinned esptool is an external build dependency")
        flash_source = FLASH.read_text(encoding="utf-8")
        for command in (
            "read_mac", "flash_id", "read_flash", "write_flash", "verify_flash"
        ):
            with self.subTest(command=command):
                self.assertIn(command, flash_source)
                result = subprocess.run(
                    [str(PINNED_ESPTOOL), command, "--help"],
                    check=False,
                    capture_output=True,
                    text=True,
                )
                self.assertEqual(result.returncode, 0, result.stderr)

    def make_delivery(self, root: pathlib.Path, identity: str) -> pathlib.Path:
        delivery = root / "delivery"
        delivery.mkdir()
        for name, content in (
            ("bootloader.bin", b"boot"),
            ("partitions.bin", b"part"),
            ("boot_app0.bin", b"ota"),
            ("application.bin", b"app"),
        ):
            artifact = delivery / name
            artifact.write_bytes(content)
            artifact.chmod(0o600)
        (delivery / "identity.txt").write_text(identity + "\n", encoding="utf-8")
        checksum = subprocess.run(
            [
                "shasum",
                "-a",
                "256",
                "bootloader.bin",
                "partitions.bin",
                "boot_app0.bin",
                "application.bin",
            ],
            cwd=delivery,
            check=True,
            capture_output=True,
            text=True,
            env={**os.environ, "LANG": "C", "LC_ALL": "C"},
        )
        (delivery / "checksums.sha256").write_text(
            checksum.stdout,
            encoding="utf-8",
        )
        (delivery / "checksums.sha256").chmod(0o600)
        return delivery

    def make_fake_esptool(self, root: pathlib.Path) -> pathlib.Path:
        tool = root / "fake-esptool.sh"
        tool.write_text(
            "#!/bin/sh\n"
            "case \" $* \" in *\\ --no-stub\\ *) ;; *) "
            "printf '%s\\n' MISSING_NO_STUB >&2; exit 10 ;; esac\n"
            "case \" $* \" in\n"
            "  *\\ read_mac\\ *) printf '%s\\n' "
            "'MAC:                AA:BB:CC:DD:EE:FF' "
            "'MAC:                AA:BB:CC:DD:EE:FF' ;;\n"
            "  *\\ flash_id\\ *) printf '%s\\n' 'Detected flash size: 16MB'; "
            "printf '%s\\n' FLASH_ID >> \"${ESPTOOL_TEST_LOG:-/dev/null}\" ;;\n"
            "  *\\ read_flash\\ *) for output do :; done; "
            "dd if=/dev/zero of=\"$output\" bs=1 count=0 seek=16777216 2>/dev/null; "
            "printf '%s\\n' READ_FLASH >> \"${ESPTOOL_TEST_LOG:-/dev/null}\" ;;\n"
            "  *\\ write_flash\\ *) printf '%s\\n' WRITE_FLASH >> "
            "\"${ESPTOOL_TEST_LOG:-/dev/null}\"; printf '%s\\n' FLASH_CALLED ;;\n"
            "  *\\ verify_flash\\ *) printf '%s\\n' VERIFY_FLASH >> "
            "\"${ESPTOOL_TEST_LOG:-/dev/null}\"; printf '%s\\n' VERIFY_CALLED ;;\n"
            "  *) exit 9 ;;\n"
            "esac\n",
            encoding="utf-8",
        )
        tool.chmod(0o700)
        return tool

    def make_short_backup_esptool(self, root: pathlib.Path) -> pathlib.Path:
        tool = root / "short-backup-esptool.sh"
        tool.write_text(
            "#!/bin/sh\n"
            "case \" $* \" in\n"
            "  *\\ read_mac\\ *) printf '%s\\n' 'MAC:                AA:BB:CC:DD:EE:FF' ;;\n"
            "  *\\ flash_id\\ *) printf '%s\\n' 'Detected flash size: 16MB' ;;\n"
            "  *\\ read_flash\\ *) for output do :; done; printf truncated > \"$output\" ;;\n"
            "  *\\ write_flash\\ *) printf '%s\\n' FLASH_CALLED ;;\n"
            "  *) exit 9 ;;\n"
            "esac\n",
            encoding="utf-8",
        )
        tool.chmod(0o700)
        return tool

    def make_failing_flash_esptool(self, root: pathlib.Path) -> pathlib.Path:
        tool = root / "failing-flash-esptool.sh"
        tool.write_text(
            "#!/bin/sh\n"
            "case \" $* \" in\n"
            "  *\\ read_mac\\ *) printf '%s\\n' 'MAC:                AA:BB:CC:DD:EE:FF' ;;\n"
            "  *\\ flash_id\\ *) printf '%s\\n' 'Detected flash size: 16MB' ;;\n"
            "  *\\ read_flash\\ *) for output do :; done; "
            "dd if=/dev/zero of=\"$output\" bs=1 count=0 seek=16777216 2>/dev/null ;;\n"
            "  *\\ write_flash\\ *) printf '%s\\n' FLASH_ATTEMPTED; exit 8 ;;\n"
            "  *) exit 9 ;;\n"
            "esac\n",
            encoding="utf-8",
        )
        tool.chmod(0o700)
        return tool

    def make_fake_identity_checker(self, root: pathlib.Path) -> pathlib.Path:
        tool = root / "fake-identity-checker.py"
        tool.write_text(
            "import sys\n"
            "expected = ['--port', sys.argv[2], '--expected-usb-serial', 'CAT-A-USB-001']\n"
            "raise SystemExit(0 if sys.argv[1:] == expected else 9)\n",
            encoding="utf-8",
        )
        return tool

    def test_checks_identity_hashes_and_mac_before_write(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            delivery = self.make_delivery(root, "cat_a")
            fake_esptool = self.make_fake_esptool(root)
            identity_checker = self.make_fake_identity_checker(root)
            fake_port = root / "tty"
            fake_port.touch()
            esptool_log = root / "esptool.log"
            backup_root = root / "backups"
            environment = {
                **os.environ,
                "LANG": "C",
                "LC_ALL": "C",
                "LC_DEVICE_BUILD_DIR": str(delivery),
                "LC_EXPECTED_DEVICE_ID": "cat_a",
                "LC_OPERATOR_CONFIRMATION": "人在板旁、供电稳定、线缆不动",
                "EXPECTED_ESP32_MAC": "AA:BB:CC:DD:EE:FF",
                "EXPECTED_ESP32_USB_SERIAL": "CAT-A-USB-001",
                "LC_ESP32_PORT": str(fake_port),
                "ESPTOOL_BIN": str(fake_esptool),
                "PYTHON_BIN": sys.executable,
                "ESP32_PORT_IDENTITY_TOOL": str(identity_checker),
                "LC_DEVICE_BINDING_REGISTRY": str(root / "bindings.json"),
                "LC_ESP32_BACKUP_ROOT": str(backup_root),
                "ESPTOOL_TEST_LOG": str(esptool_log),
            }
            result = subprocess.run(
                [str(FLASH)],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("FLASH_CALLED", result.stdout)
            self.assertEqual(
                esptool_log.read_text(encoding="utf-8"),
                "FLASH_ID\nREAD_FLASH\nWRITE_FLASH\nVERIFY_FLASH\n",
            )
            self.assertIn("VERIFY_CALLED", result.stdout)
            backups = list((backup_root / "cat_a").glob("*.bin"))
            checksums = list((backup_root / "cat_a").glob("*.sha256"))
            self.assertEqual(len(backups), 1)
            self.assertEqual(len(checksums), 1)
            self.assertEqual(backups[0].stat().st_size, 0x1000000)
            binding = (root / "bindings.json").read_text(encoding="utf-8")
            self.assertIn('"cat_a"', binding)
            self.assertIn('"CAT-A-USB-001"', binding)

    def test_refuses_a_truncated_rollback_backup_before_write(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            delivery = self.make_delivery(root, "cat_a")
            fake_esptool = self.make_short_backup_esptool(root)
            identity_checker = self.make_fake_identity_checker(root)
            fake_port = root / "tty"
            fake_port.touch()
            backup_root = root / "backups"
            environment = {
                **os.environ,
                "LANG": "C",
                "LC_ALL": "C",
                "LC_DEVICE_BUILD_DIR": str(delivery),
                "LC_EXPECTED_DEVICE_ID": "cat_a",
                "LC_OPERATOR_CONFIRMATION": "人在板旁、供电稳定、线缆不动",
                "EXPECTED_ESP32_MAC": "AA:BB:CC:DD:EE:FF",
                "EXPECTED_ESP32_USB_SERIAL": "CAT-A-USB-001",
                "LC_ESP32_PORT": str(fake_port),
                "ESPTOOL_BIN": str(fake_esptool),
                "PYTHON_BIN": sys.executable,
                "ESP32_PORT_IDENTITY_TOOL": str(identity_checker),
                "LC_DEVICE_BINDING_REGISTRY": str(root / "bindings.json"),
                "LC_ESP32_BACKUP_ROOT": str(backup_root),
            }

            result = subprocess.run(
                [str(FLASH)],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn("FLASH_CALLED", result.stdout)
            self.assertFalse(list(backup_root.glob("**/*.bin")))

    def test_refuses_a_delivery_for_the_other_cat(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            delivery = self.make_delivery(root, "cat_b")
            fake_esptool = self.make_fake_esptool(root)
            identity_checker = self.make_fake_identity_checker(root)
            fake_port = root / "tty"
            fake_port.touch()
            environment = {
                **os.environ,
                "LANG": "C",
                "LC_ALL": "C",
                "LC_DEVICE_BUILD_DIR": str(delivery),
                "LC_EXPECTED_DEVICE_ID": "cat_a",
                "LC_OPERATOR_CONFIRMATION": "人在板旁、供电稳定、线缆不动",
                "EXPECTED_ESP32_MAC": "AA:BB:CC:DD:EE:FF",
                "EXPECTED_ESP32_USB_SERIAL": "CAT-A-USB-001",
                "LC_ESP32_PORT": str(fake_port),
                "ESPTOOL_BIN": str(fake_esptool),
                "PYTHON_BIN": sys.executable,
                "ESP32_PORT_IDENTITY_TOOL": str(identity_checker),
                "LC_DEVICE_BINDING_REGISTRY": str(root / "bindings.json"),
            }
            result = subprocess.run(
                [str(FLASH)],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )

            self.assertEqual(result.returncode, 3)
            self.assertNotIn("FLASH_CALLED", result.stdout)

    def test_refuses_a_manifest_that_does_not_cover_every_flash_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            delivery = self.make_delivery(root, "cat_a")
            incomplete = subprocess.run(
                [
                    "shasum", "-a", "256",
                    "bootloader.bin", "partitions.bin", "boot_app0.bin",
                ],
                cwd=delivery,
                check=True,
                capture_output=True,
                text=True,
            ).stdout
            (delivery / "checksums.sha256").write_text(
                incomplete,
                encoding="utf-8",
            )
            fake_esptool = self.make_fake_esptool(root)
            identity_checker = self.make_fake_identity_checker(root)
            fake_port = root / "tty"
            fake_port.touch()
            environment = {
                **os.environ,
                "LANG": "C",
                "LC_ALL": "C",
                "LC_DEVICE_BUILD_DIR": str(delivery),
                "LC_EXPECTED_DEVICE_ID": "cat_a",
                "LC_OPERATOR_CONFIRMATION": "人在板旁、供电稳定、线缆不动",
                "EXPECTED_ESP32_MAC": "AA:BB:CC:DD:EE:FF",
                "EXPECTED_ESP32_USB_SERIAL": "CAT-A-USB-001",
                "LC_ESP32_PORT": str(fake_port),
                "ESPTOOL_BIN": str(fake_esptool),
                "PYTHON_BIN": sys.executable,
                "ESP32_PORT_IDENTITY_TOOL": str(identity_checker),
                "LC_DEVICE_BINDING_REGISTRY": str(root / "bindings.json"),
                "LC_ESP32_BACKUP_ROOT": str(root / "backups"),
            }

            result = subprocess.run(
                [str(FLASH)],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn("FLASH_CALLED", result.stdout)

    def test_refuses_before_device_access_without_fresh_operator_confirmation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            delivery = self.make_delivery(root, "cat_a")
            fake_esptool = self.make_fake_esptool(root)
            fake_port = root / "tty"
            fake_port.touch()
            environment = {
                **os.environ,
                "LANG": "C",
                "LC_ALL": "C",
                "LC_DEVICE_BUILD_DIR": str(delivery),
                "LC_EXPECTED_DEVICE_ID": "cat_a",
                "LC_OPERATOR_CONFIRMATION": "人在板旁",
                "EXPECTED_ESP32_MAC": "AA:BB:CC:DD:EE:FF",
                "LC_ESP32_PORT": str(fake_port),
                "ESPTOOL_BIN": str(fake_esptool),
                "LC_DEVICE_BINDING_REGISTRY": str(root / "bindings.json"),
            }
            result = subprocess.run(
                [str(FLASH)],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, "")
            self.assertIn("confirmation", result.stderr.lower())

    def test_usb_metadata_failure_prevents_mac_read_and_flash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            delivery = self.make_delivery(root, "cat_a")
            fake_esptool = self.make_fake_esptool(root)
            identity_checker = root / "reject-identity.py"
            identity_checker.write_text("raise SystemExit(7)\n", encoding="utf-8")
            fake_port = root / "tty"
            fake_port.touch()
            environment = {
                **os.environ,
                "LANG": "C",
                "LC_ALL": "C",
                "LC_DEVICE_BUILD_DIR": str(delivery),
                "LC_EXPECTED_DEVICE_ID": "cat_a",
                "LC_OPERATOR_CONFIRMATION": "人在板旁、供电稳定、线缆不动",
                "EXPECTED_ESP32_MAC": "AA:BB:CC:DD:EE:FF",
                "EXPECTED_ESP32_USB_SERIAL": "CAT-A-USB-001",
                "LC_ESP32_PORT": str(fake_port),
                "ESPTOOL_BIN": str(fake_esptool),
                "PYTHON_BIN": sys.executable,
                "ESP32_PORT_IDENTITY_TOOL": str(identity_checker),
                "LC_DEVICE_BINDING_REGISTRY": str(root / "bindings.json"),
            }
            result = subprocess.run(
                [str(FLASH)],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )

            self.assertEqual(result.returncode, 7)
            self.assertNotIn("MAC:", result.stdout)
            self.assertNotIn("FLASH_CALLED", result.stdout)

    def test_invalid_expected_mac_is_rejected_before_esptool(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            delivery = self.make_delivery(root, "cat_a")
            fake_esptool = self.make_fake_esptool(root)
            identity_checker = self.make_fake_identity_checker(root)
            fake_port = root / "tty"
            fake_port.touch()
            environment = {
                **os.environ,
                "LANG": "C",
                "LC_ALL": "C",
                "LC_DEVICE_BUILD_DIR": str(delivery),
                "LC_EXPECTED_DEVICE_ID": "cat_a",
                "LC_OPERATOR_CONFIRMATION": "人在板旁、供电稳定、线缆不动",
                "EXPECTED_ESP32_MAC": "not-a-mac",
                "EXPECTED_ESP32_USB_SERIAL": "CAT-A-USB-001",
                "LC_ESP32_PORT": str(fake_port),
                "ESPTOOL_BIN": str(fake_esptool),
                "PYTHON_BIN": sys.executable,
                "ESP32_PORT_IDENTITY_TOOL": str(identity_checker),
                "LC_DEVICE_BINDING_REGISTRY": str(root / "bindings.json"),
            }
            result = subprocess.run(
                [str(FLASH)],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, "")
            self.assertIn("mac", result.stderr.lower())

    def test_refuses_reusing_one_physical_board_for_the_other_cat(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            delivery = self.make_delivery(root, "cat_b")
            fake_esptool = self.make_fake_esptool(root)
            identity_checker = self.make_fake_identity_checker(root)
            fake_port = root / "tty"
            fake_port.touch()
            registry = root / "bindings.json"
            registry.write_text(
                '{"version":1,"bindings":{"cat_a":'
                '{"usb_serial":"CAT-A-USB-001",'
                '"mac":"aa:bb:cc:dd:ee:ff"}}}\n',
                encoding="utf-8",
            )
            registry.chmod(0o600)
            environment = {
                **os.environ,
                "LANG": "C",
                "LC_ALL": "C",
                "LC_DEVICE_BUILD_DIR": str(delivery),
                "LC_EXPECTED_DEVICE_ID": "cat_b",
                "LC_OPERATOR_CONFIRMATION": "人在板旁、供电稳定、线缆不动",
                "EXPECTED_ESP32_MAC": "AA:BB:CC:DD:EE:FF",
                "EXPECTED_ESP32_USB_SERIAL": "CAT-A-USB-001",
                "LC_ESP32_PORT": str(fake_port),
                "ESPTOOL_BIN": str(fake_esptool),
                "PYTHON_BIN": sys.executable,
                "ESP32_PORT_IDENTITY_TOOL": str(identity_checker),
                "LC_DEVICE_BINDING_REGISTRY": str(registry),
                "LC_ESP32_BACKUP_ROOT": str(root / "backups"),
            }
            result = subprocess.run(
                [str(FLASH)],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn("MAC:", result.stdout)
            self.assertNotIn("FLASH_CALLED", result.stdout)
            self.assertIn("binding rejected", result.stderr.lower())

    def test_reserves_physical_identity_before_a_failed_flash_attempt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            delivery = self.make_delivery(root, "cat_a")
            fake_esptool = self.make_failing_flash_esptool(root)
            identity_checker = self.make_fake_identity_checker(root)
            fake_port = root / "tty"
            fake_port.touch()
            registry = root / "bindings.json"
            environment = {
                **os.environ,
                "LANG": "C",
                "LC_ALL": "C",
                "LC_DEVICE_BUILD_DIR": str(delivery),
                "LC_EXPECTED_DEVICE_ID": "cat_a",
                "LC_OPERATOR_CONFIRMATION": "人在板旁、供电稳定、线缆不动",
                "EXPECTED_ESP32_MAC": "AA:BB:CC:DD:EE:FF",
                "EXPECTED_ESP32_USB_SERIAL": "CAT-A-USB-001",
                "LC_ESP32_PORT": str(fake_port),
                "ESPTOOL_BIN": str(fake_esptool),
                "PYTHON_BIN": sys.executable,
                "ESP32_PORT_IDENTITY_TOOL": str(identity_checker),
                "LC_DEVICE_BINDING_REGISTRY": str(registry),
            }
            result = subprocess.run(
                [str(FLASH)],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )

            self.assertEqual(result.returncode, 8)
            self.assertIn("FLASH_ATTEMPTED", result.stdout)
            binding = registry.read_text(encoding="utf-8")
            self.assertIn('"cat_a"', binding)
            self.assertIn('"CAT-A-USB-001"', binding)


if __name__ == "__main__":
    unittest.main()
