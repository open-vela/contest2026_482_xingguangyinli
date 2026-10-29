#!/usr/bin/env python3
"""Fail closed unless an ESP32 delivery exactly matches its private manifest."""

from __future__ import annotations

import argparse
import hashlib
import hmac
import pathlib
import re
import stat


ARTIFACTS = (
    "bootloader.bin",
    "partitions.bin",
    "boot_app0.bin",
    "application.bin",
)
MAX_ARTIFACT_SIZES = {
    "bootloader.bin": 0x8000,
    "partitions.bin": 0x6000,
    "boot_app0.bin": 0x2000,
    "application.bin": 0x600000,
}
HEX_SHA256 = re.compile(r"^[0-9a-f]{64}$")


def digest(path: pathlib.Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def verify(delivery: pathlib.Path) -> None:
    delivery = pathlib.Path(delivery)
    if delivery.is_symlink() or not delivery.is_dir():
        raise ValueError("delivery_unsafe_directory")
    manifest = delivery / "checksums.sha256"
    if manifest.is_symlink() or not manifest.is_file():
        raise ValueError("delivery_manifest_missing")
    try:
        lines = manifest.read_text(encoding="ascii").splitlines()
    except (OSError, UnicodeError) as error:
        raise ValueError("delivery_manifest_unreadable") from error
    if len(lines) != len(ARTIFACTS):
        raise ValueError("delivery_manifest_wrong_count")

    for expected_name, line in zip(ARTIFACTS, lines):
        parts = line.split("  ")
        if (
            len(parts) != 2
            or parts[1] != expected_name
            or HEX_SHA256.fullmatch(parts[0]) is None
        ):
            raise ValueError("delivery_manifest_invalid_entry")
        artifact = delivery / expected_name
        if artifact.is_symlink() or not artifact.is_file():
            raise ValueError("delivery_artifact_unsafe")
        metadata = artifact.stat()
        if metadata.st_size <= 0 or stat.S_IMODE(metadata.st_mode) & 0o077:
            raise ValueError("delivery_artifact_permissions_or_size")
        if metadata.st_size > MAX_ARTIFACT_SIZES[expected_name]:
            raise ValueError("delivery_artifact_too_large")
        if not hmac.compare_digest(digest(artifact), parts[0]):
            raise ValueError("delivery_artifact_hash_mismatch")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("delivery", type=pathlib.Path)
    args = parser.parse_args()
    try:
        verify(args.delivery)
    except (OSError, ValueError):
        print("ESP32 device delivery rejected", file=__import__("sys").stderr)
        return 2
    print("ESP32_DEVICE_DELIVERY_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
