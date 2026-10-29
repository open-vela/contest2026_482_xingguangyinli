#!/usr/bin/env python3
"""Plan or atomically apply the private-LAN Gemini S1 runtime generation.

The command accepts secrets only through locked-down files. Output contains
only static status codes, byte lengths and SHA-256 digests. The default is
read-only planning; device mutation requires both ``--apply`` and an explicit
confirmation that the Living Canvas application is stopped.
"""

from __future__ import annotations

import argparse
import hashlib
import ipaddress
import json
import os
import pathlib
import re
import stat
import subprocess
import sys
import tempfile
import urllib.parse


REMOTE_ROOT = "/data/living_canvas"
REMOTE_TARGETS = {
    "backend_url": f"{REMOTE_ROOT}/backend_url",
    "voice_device_token": f"{REMOTE_ROOT}/voice_device_token",
    "accessory_device_token": f"{REMOTE_ROOT}/accessory_device_token",
    "local_accessory_keys": f"{REMOTE_ROOT}/local_accessory_keys",
}
RFC1918 = tuple(
    ipaddress.ip_network(value)
    for value in ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16")
)
TOKEN_PATTERN = re.compile(rb"[-A-Za-z0-9._]{32,128}")
PAIRING_PATTERN = re.compile(
    rb"cat_a=([0-9a-f]{64})\ncat_b=([0-9a-f]{64})\n"
)
SERIAL_PATTERN = re.compile(r"[A-Za-z0-9._:-]{1,128}")


class ConfigurationError(Exception):
    """Safe static error reason; never construct this with secret data."""


def _digest(content: bytes) -> str:
    return hashlib.sha256(content).hexdigest()


def validate_backend_url(value: str) -> bytes:
    if not isinstance(value, str) or any(ord(character) <= 0x20 for character in value):
        raise ConfigurationError("invalid_backend_url")
    try:
        parsed = urllib.parse.urlsplit(value)
        address = ipaddress.ip_address(parsed.hostname or "")
        port = parsed.port
    except ValueError:
        raise ConfigurationError("invalid_backend_url") from None
    if (
        parsed.scheme != "http"
        or address.version != 4
        or not any(address in network for network in RFC1918)
        or parsed.hostname != str(address)
        or parsed.username is not None
        or parsed.password is not None
        or port is None
        or not 1024 <= port <= 65535
        or parsed.path
        or parsed.query
        or parsed.fragment
        or value != f"http://{address}:{port}"
    ):
        raise ConfigurationError("invalid_backend_url")
    return (value + "\n").encode("ascii")


def read_private_file(path: pathlib.Path, *, max_bytes: int = 512,
                      line_count: int = 1) -> bytes:
    path = pathlib.Path(path)
    if not path.is_absolute() or path.is_symlink():
        raise ConfigurationError("invalid_private_file")
    descriptor = -1
    try:
        flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(path, flags)
        metadata = os.fstat(descriptor)
        if (
            not stat.S_ISREG(metadata.st_mode)
            or stat.S_IMODE(metadata.st_mode) & 0o077
            or metadata.st_size <= 0
            or metadata.st_size > max_bytes
        ):
            raise ConfigurationError("invalid_private_file")
        with os.fdopen(descriptor, "rb", closefd=True) as stream:
            descriptor = -1
            content = stream.read(max_bytes + 1)
    except ConfigurationError:
        raise
    except OSError:
        raise ConfigurationError("invalid_private_file") from None
    finally:
        if descriptor >= 0:
            os.close(descriptor)
    if (
        len(content) > max_bytes
        or not content.endswith(b"\n")
        or b"\r" in content
        or content.count(b"\n") != line_count
        or any(
            byte != 0x0A and (byte < 0x20 or byte > 0x7E)
            for byte in content.rstrip(b"\n")
        )
    ):
        raise ConfigurationError("invalid_private_file")
    return content


def load_config(backend_url: str, owner_token_file: pathlib.Path,
                accessory_token_file: pathlib.Path,
                local_keys_file: pathlib.Path) -> dict[str, bytes]:
    backend = validate_backend_url(backend_url)
    owner = read_private_file(owner_token_file, max_bytes=130).rstrip(b"\n")
    accessory = read_private_file(accessory_token_file, max_bytes=130).rstrip(b"\n")
    keys = read_private_file(local_keys_file, max_bytes=142, line_count=2)

    if (
        not owner.startswith(b"lc-owner-")
        or TOKEN_PATTERN.fullmatch(owner) is None
        or owner.startswith((b"sk-", b"tp-", b"ttp-"))
    ):
        raise ConfigurationError("invalid_owner_token")
    if owner == accessory:
        raise ConfigurationError("reused_secret")
    if (
        TOKEN_PATTERN.fullmatch(accessory) is None
        or accessory.startswith((b"sk-", b"tp-", b"ttp-", b"lc-owner-"))
    ):
        raise ConfigurationError("invalid_accessory_token")
    pairing = PAIRING_PATTERN.fullmatch(keys)
    if pairing is None:
        raise ConfigurationError("invalid_local_keys")
    if pairing.group(1) == pairing.group(2):
        raise ConfigurationError("reused_secret")

    return {
        "backend_url": backend,
        "voice_device_token": owner + b"\n",
        "accessory_device_token": accessory + b"\n",
        "local_accessory_keys": keys,
    }


def _plan_document(config: dict[str, bytes]) -> dict:
    return {
        "status": "PLAN",
        "mutations": "NONE",
        "targets": {
            name: {"bytes": len(config[name]), "sha256": _digest(config[name])}
            for name in REMOTE_TARGETS
        },
    }


def _new_backup_directory(path: pathlib.Path) -> pathlib.Path:
    path = pathlib.Path(path)
    if not path.is_absolute() or path.exists() or path.is_symlink():
        raise ConfigurationError("invalid_backup_dir")
    try:
        path.mkdir(mode=0o700, parents=False)
        path.chmod(0o700)
    except OSError:
        raise ConfigurationError("invalid_backup_dir") from None
    return path


def _write_backup(path: pathlib.Path, content: bytes) -> None:
    descriptor = os.open(
        path,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_CLOEXEC", 0),
        0o600,
    )
    try:
        with os.fdopen(descriptor, "wb", closefd=True) as stream:
            descriptor = -1
            stream.write(content)
            stream.flush()
            os.fsync(stream.fileno())
    finally:
        if descriptor >= 0:
            os.close(descriptor)
    path.chmod(0o600)


def _rollback(device, previous: dict[str, bytes | None]) -> None:
    try:
        for name, target in REMOTE_TARGETS.items():
            device.restore(target, previous[name])
        for name, target in REMOTE_TARGETS.items():
            if device.read(target) != previous[name]:
                raise ConfigurationError("rollback_readback_failed")
    except Exception:
        raise ConfigurationError("rollback_failed") from None


def apply_transaction(device, config: dict[str, bytes], backup_dir: pathlib.Path,
                      *, apply: bool, app_stopped: bool) -> str:
    device.verify_target()
    if not apply:
        print(json.dumps(_plan_document(config), sort_keys=True))
        return "PLAN"
    if not app_stopped:
        raise ConfigurationError("application_state_unconfirmed")

    backup = _new_backup_directory(backup_dir)
    previous: dict[str, bytes | None] = {}
    manifest = {"status": "BACKUP", "targets": {}}
    for name, target in REMOTE_TARGETS.items():
        content = device.read(target)
        previous[name] = content
        if content is None:
            manifest["targets"][name] = {"state": "ABSENT"}
        else:
            _write_backup(backup / f"{name}.previous", content)
            manifest["targets"][name] = {
                "state": "PRESENT",
                "bytes": len(content),
                "sha256": _digest(content),
            }
    _write_backup(
        backup / "manifest.json",
        (json.dumps(manifest, sort_keys=True) + "\n").encode("ascii"),
    )

    staged = []
    try:
        for name, target in REMOTE_TARGETS.items():
            next_target = target + ".next"
            device.stage(next_target, config[name])
            staged.append(next_target)
            if device.read(next_target) != config[name]:
                raise ConfigurationError("staging_readback_mismatch")
    except Exception:
        for remote in staged:
            device.cleanup(remote)
        raise

    try:
        for name, target in REMOTE_TARGETS.items():
            device.replace(target + ".next", target)
            if device.read(target) != config[name]:
                raise ConfigurationError("formal_readback_mismatch")
    except Exception as original:
        _rollback(device, previous)
        for target in REMOTE_TARGETS.values():
            device.cleanup(target + ".next")
        raise original

    for target in REMOTE_TARGETS.values():
        device.cleanup(target + ".next")
    print(json.dumps({"status": "APPLIED", "readback": "VERIFIED"}, sort_keys=True))
    return "APPLIED"


class AdbDevice:
    def __init__(self, adb: pathlib.Path, serial: str, timeout: float = 10.0):
        self.adb = str(adb)
        self.serial = serial
        self.timeout = timeout

    def _run(self, *arguments: str, target: bool = True) -> subprocess.CompletedProcess:
        command = [self.adb]
        if target:
            command.extend(("-s", self.serial))
        command.extend(arguments)
        try:
            return subprocess.run(
                command,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=self.timeout,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired):
            raise ConfigurationError("adb_unavailable") from None

    def verify_target(self) -> None:
        if SERIAL_PATTERN.fullmatch(self.serial) is None:
            raise ConfigurationError("invalid_serial")
        result = self._run("devices", "-l", target=False)
        if result.returncode != 0:
            raise ConfigurationError("adb_unavailable")
        devices = []
        for raw_line in result.stdout.decode("utf-8", "replace").splitlines()[1:]:
            fields = raw_line.split()
            if len(fields) >= 2 and fields[1] == "device":
                devices.append(fields[0])
        if devices != [self.serial]:
            raise ConfigurationError("adb_target_mismatch")

    def _exists(self, remote: str) -> bool:
        result = self._run("shell", "ls", "-l", remote)
        output = result.stdout.decode("utf-8", "replace").strip()
        if result.returncode == 0 and output == "nsh: ls: stat failed: 2":
            return False
        if result.returncode == 0 and output and not output.startswith("nsh:"):
            return True
        raise ConfigurationError("device_read_failed")

    def read(self, remote: str) -> bytes | None:
        if not self._exists(remote):
            return None
        with tempfile.TemporaryDirectory(prefix="lc-config-read-") as directory:
            local = pathlib.Path(directory) / "value"
            result = self._run("pull", remote, str(local))
            if result.returncode != 0 or not local.is_file():
                raise ConfigurationError("device_read_failed")
            try:
                return local.read_bytes()
            except OSError:
                raise ConfigurationError("device_read_failed") from None

    def stage(self, remote: str, content: bytes) -> None:
        with tempfile.TemporaryDirectory(prefix="lc-config-stage-") as directory:
            local = pathlib.Path(directory) / "value"
            _write_backup(local, content)
            result = self._run("push", str(local), remote)
        if result.returncode != 0 or self._run("shell", "chmod", "600", remote).returncode != 0:
            raise ConfigurationError("device_stage_failed")

    def replace(self, staged: str, target: str) -> None:
        if self._run("shell", "mv", staged, target).returncode != 0:
            raise ConfigurationError("device_replace_failed")

    def restore(self, target: str, previous: bytes | None) -> None:
        if previous is None:
            if self._run("shell", "rm", "-f", target).returncode != 0:
                raise ConfigurationError("device_restore_failed")
            return
        rollback = target + ".rollback"
        self.stage(rollback, previous)
        self.replace(rollback, target)

    def cleanup(self, remote: str) -> None:
        self._run("shell", "rm", "-f", remote)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", type=pathlib.Path, default=pathlib.Path("adb"))
    parser.add_argument("--serial", required=True)
    parser.add_argument("--backend-url", required=True)
    parser.add_argument("--owner-token-file", type=pathlib.Path, required=True)
    parser.add_argument("--accessory-token-file", type=pathlib.Path, required=True)
    parser.add_argument("--local-keys-file", type=pathlib.Path, required=True)
    parser.add_argument("--backup-dir", type=pathlib.Path, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--plan", action="store_true")
    mode.add_argument("--apply", action="store_true")
    parser.add_argument("--app-stopped-confirmed", action="store_true")
    return parser


def main() -> int:
    args = _parser().parse_args()
    os.umask(0o077)
    try:
        config = load_config(
            args.backend_url,
            args.owner_token_file,
            args.accessory_token_file,
            args.local_keys_file,
        )
        device = AdbDevice(args.adb, args.serial)
        apply_transaction(
            device,
            config,
            args.backup_dir,
            apply=args.apply,
            app_stopped=args.app_stopped_confirmed,
        )
    except ConfigurationError as error:
        print(json.dumps({"status": "FAIL", "reason": str(error)}, sort_keys=True))
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
