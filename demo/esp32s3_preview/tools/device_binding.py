#!/usr/bin/env python3
"""Persist a one-to-one mapping between physical ESP32 boards and cat slots."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import re
import tempfile


DEVICE_IDS = {"cat_a", "cat_b"}
USB_SERIAL = re.compile(r"^[A-Za-z0-9:._-]{1,64}$")
MAC = re.compile(r"^[0-9a-f]{2}(?::[0-9a-f]{2}){5}$")


def _normalized_inputs(
    device_id: str, usb_serial: str, mac: str
) -> tuple[str, str, str]:
    if device_id not in DEVICE_IDS:
        raise ValueError("invalid_device_id")
    if not isinstance(usb_serial, str) or USB_SERIAL.fullmatch(usb_serial) is None:
        raise ValueError("invalid_usb_serial")
    if not isinstance(mac, str):
        raise ValueError("invalid_mac")
    normalized_mac = mac.lower()
    if MAC.fullmatch(normalized_mac) is None:
        raise ValueError("invalid_mac")
    return device_id, usb_serial, normalized_mac


def _load_registry(registry: pathlib.Path) -> dict[str, dict[str, str]]:
    registry = pathlib.Path(registry)
    if not registry.parent.is_dir():
        raise ValueError("registry_parent_missing")
    if not registry.exists():
        return {}
    if registry.is_symlink() or not registry.is_file():
        raise ValueError("registry_unsafe_file")
    if registry.stat().st_mode & 0o077:
        raise ValueError("registry_permissions_too_open")

    try:
        payload = json.loads(registry.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ValueError("registry_invalid_json") from error
    if (
        not isinstance(payload, dict)
        or set(payload) != {"version", "bindings"}
        or payload["version"] != 1
        or not isinstance(payload["bindings"], dict)
    ):
        raise ValueError("registry_invalid_schema")

    normalized: dict[str, dict[str, str]] = {}
    seen_serials: set[str] = set()
    seen_macs: set[str] = set()
    for device_id, binding in payload["bindings"].items():
        if not isinstance(binding, dict) or set(binding) != {"usb_serial", "mac"}:
            raise ValueError("registry_invalid_binding")
        checked_id, checked_serial, checked_mac = _normalized_inputs(
            device_id, binding["usb_serial"], binding["mac"]
        )
        if checked_serial in seen_serials or checked_mac in seen_macs:
            raise ValueError("registry_duplicate_physical_board")
        seen_serials.add(checked_serial)
        seen_macs.add(checked_mac)
        normalized[checked_id] = {
            "usb_serial": checked_serial,
            "mac": checked_mac,
        }
    return normalized


def check_binding(
    registry: pathlib.Path, device_id: str, usb_serial: str, mac: str
) -> None:
    device_id, usb_serial, mac = _normalized_inputs(
        device_id, usb_serial, mac
    )
    bindings = _load_registry(pathlib.Path(registry))
    expected = {"usb_serial": usb_serial, "mac": mac}
    current = bindings.get(device_id)
    if current is not None and current != expected:
        raise ValueError("device_id_already_bound")

    for bound_id, binding in bindings.items():
        if bound_id != device_id and (
            binding["usb_serial"] == usb_serial or binding["mac"] == mac
        ):
            raise ValueError("physical_board_already_bound")


def record_binding(
    registry: pathlib.Path, device_id: str, usb_serial: str, mac: str
) -> None:
    registry = pathlib.Path(registry)
    device_id, usb_serial, mac = _normalized_inputs(
        device_id, usb_serial, mac
    )
    check_binding(registry, device_id, usb_serial, mac)
    bindings = _load_registry(registry)
    bindings[device_id] = {"usb_serial": usb_serial, "mac": mac}
    payload = {"version": 1, "bindings": bindings}

    temporary_path: pathlib.Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=registry.parent,
            prefix=f".{registry.stem}.",
            delete=False,
        ) as temporary:
            temporary_path = pathlib.Path(temporary.name)
            json.dump(payload, temporary, ensure_ascii=True, sort_keys=True)
            temporary.write("\n")
            temporary.flush()
            os.fsync(temporary.fileno())
            os.fchmod(temporary.fileno(), 0o600)
        os.replace(temporary_path, registry)
        os.chmod(registry, 0o600)
        temporary_path = None
    finally:
        if temporary_path is not None:
            try:
                temporary_path.unlink()
            except FileNotFoundError:
                pass


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("check", "record"))
    parser.add_argument("--registry", required=True, type=pathlib.Path)
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--usb-serial", required=True)
    parser.add_argument("--mac", required=True)
    args = parser.parse_args()
    try:
        if args.mode == "check":
            check_binding(
                args.registry, args.device_id, args.usb_serial, args.mac
            )
            print("ESP32_DEVICE_BINDING_OK")
        else:
            record_binding(
                args.registry, args.device_id, args.usb_serial, args.mac
            )
            print("ESP32_DEVICE_BINDING_RECORDED")
    except (OSError, UnicodeError, ValueError):
        print("ESP32 device binding rejected", file=os.sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
