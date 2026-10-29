import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
RESTORE = ROOT / "tools" / "restore_verified_device.sh"
FLASH_SIZE = 16 * 1024 * 1024


class RestoreGuardTest(unittest.TestCase):
    def test_uses_esptool_412_flash_option_names(self):
        restore_source = RESTORE.read_text(encoding="utf-8")
        for option in ("--flash_mode", "--flash_freq", "--flash_size"):
            self.assertIn(option, restore_source)
        for obsolete in ("--flash-mode", "--flash-freq", "--flash-size"):
            self.assertNotIn(obsolete, restore_source)

    def make_backup(self, root: pathlib.Path) -> pathlib.Path:
        backup_dir = root / "backups" / "cat_a"
        backup_dir.mkdir(parents=True)
        backup = backup_dir / "20261024T000000Z-aa-bb-cc-dd-ee-ff.bin"
        with backup.open("wb") as stream:
            stream.truncate(FLASH_SIZE)
        backup.chmod(0o600)
        digest = subprocess.run(
            ["shasum", "-a", "256", backup.name],
            cwd=backup_dir,
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        checksum = pathlib.Path(str(backup) + ".sha256")
        checksum.write_text(digest, encoding="ascii")
        checksum.chmod(0o600)
        return backup

    def make_registry(self, root: pathlib.Path) -> pathlib.Path:
        registry = root / "bindings.json"
        registry.write_text(
            '{"version":1,"bindings":{"cat_a":'
            '{"usb_serial":"CAT-A-USB-001",'
            '"mac":"aa:bb:cc:dd:ee:ff"}}}\n',
            encoding="utf-8",
        )
        registry.chmod(0o600)
        return registry

    def make_identity_checker(self, root: pathlib.Path) -> pathlib.Path:
        checker = root / "identity.py"
        checker.write_text("raise SystemExit(0)\n", encoding="utf-8")
        return checker

    def make_esptool(self, root: pathlib.Path) -> pathlib.Path:
        tool = root / "esptool.sh"
        tool.write_text(
            "#!/bin/sh\n"
            "case \" $* \" in *\\ --no-stub\\ *) ;; *) "
            "printf '%s\\n' MISSING_NO_STUB >&2; exit 10 ;; esac\n"
            "case \" $* \" in\n"
            "  *\\ read_mac\\ *) printf '%s\\n' "
            "'MAC:                AA:BB:CC:DD:EE:FF' "
            "'MAC:                AA:BB:CC:DD:EE:FF' ;;\n"
            "  *\\ flash_id\\ *) printf '%s\\n' 'Detected flash size: 16MB'; "
            "printf '%s\\n' FLASH_ID >> \"$ESPTOOL_TEST_LOG\" ;;\n"
            "  *\\ write_flash\\ *) printf '%s\\n' RESTORE_WRITE >> \"$ESPTOOL_TEST_LOG\" ;;\n"
            "  *\\ verify_flash\\ *) printf '%s\\n' RESTORE_VERIFY >> \"$ESPTOOL_TEST_LOG\" ;;\n"
            "  *) exit 9 ;;\n"
            "esac\n",
            encoding="utf-8",
        )
        tool.chmod(0o700)
        return tool

    def environment(self, root: pathlib.Path, backup: pathlib.Path) -> dict:
        port = root / "tty"
        port.touch()
        return {
            **os.environ,
            "LANG": "C",
            "LC_ALL": "C",
            "LC_OPERATOR_CONFIRMATION": "人在板旁、供电稳定、线缆不动",
            "LC_EXPECTED_DEVICE_ID": "cat_a",
            "EXPECTED_ESP32_MAC": "AA:BB:CC:DD:EE:FF",
            "EXPECTED_ESP32_USB_SERIAL": "CAT-A-USB-001",
            "LC_ESP32_PORT": str(port),
            "LC_ESP32_BACKUP_FILE": str(backup),
            "LC_DEVICE_BINDING_REGISTRY": str(self.make_registry(root)),
            "ESP32_PORT_IDENTITY_TOOL": str(self.make_identity_checker(root)),
            "ESP32_DEVICE_BINDING_TOOL": str(ROOT / "tools" / "device_binding.py"),
            "ESPTOOL_BIN": str(self.make_esptool(root)),
            "ESPTOOL_TEST_LOG": str(root / "esptool.log"),
            "PYTHON_BIN": sys.executable,
        }

    def test_restores_only_after_identity_size_and_hash_checks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            backup = self.make_backup(root)
            environment = self.environment(root, backup)

            result = subprocess.run(
                [str(RESTORE)],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                pathlib.Path(environment["ESPTOOL_TEST_LOG"]).read_text(
                    encoding="utf-8"
                ),
                "FLASH_ID\nRESTORE_WRITE\nRESTORE_VERIFY\n",
            )
            self.assertEqual(stat.S_IMODE(backup.stat().st_mode), 0o600)
            self.assertIn("ESP32_RESTORE_VERIFY_OK", result.stdout)

    def test_rejects_tampered_backup_before_device_access(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            backup = self.make_backup(root)
            with backup.open("r+b") as stream:
                stream.write(b"tampered")
            environment = self.environment(root, backup)

            result = subprocess.run(
                [str(RESTORE)],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(
                pathlib.Path(environment["ESPTOOL_TEST_LOG"]).exists()
            )

    def test_rejects_a_world_readable_backup_before_device_access(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            backup = self.make_backup(root)
            backup.chmod(0o644)
            environment = self.environment(root, backup)

            result = subprocess.run(
                [str(RESTORE)],
                check=False,
                capture_output=True,
                text=True,
                env=environment,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(
                pathlib.Path(environment["ESPTOOL_TEST_LOG"]).exists()
            )


if __name__ == "__main__":
    unittest.main()
