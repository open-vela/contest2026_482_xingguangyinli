#!/usr/bin/env python3
"""Verify one cat board's USB metadata before any device I/O."""

from __future__ import annotations

import argparse
import os
import re
import sys
from typing import Iterable


EXPECTED_VID = 0x303A
CALLOUT_PATH = re.compile(r"^/dev/cu\.[A-Za-z0-9._-]{1,128}$")
USB_SERIAL = re.compile(r"^[A-Za-z0-9:._-]{1,64}$")


def configured_non_cat_serials(environ=None) -> frozenset[str]:
    source = os.environ if environ is None else environ
    raw = source.get("LIVING_CANVAS_NON_CAT_USB_SERIALS", "")
    values = tuple(part.strip() for part in raw.split(",") if part.strip())
    if not values or any(USB_SERIAL.fullmatch(value) is None for value in values):
        raise ValueError("non-cat USB identities are not configured")
    return frozenset(values)


def verify_port_identity(
    ports: Iterable[object], device: str, expected_serial: str,
    reserved_serials: Iterable[str],
) -> str:
    reserved = frozenset(reserved_serials)
    if (not reserved or
            any(not isinstance(value, str) or
                USB_SERIAL.fullmatch(value) is None for value in reserved)):
        raise ValueError("non-cat USB identities are invalid")
    if not isinstance(device, str) or CALLOUT_PATH.fullmatch(device) is None:
        raise ValueError("ESP32 callout path is invalid")
    if (
        not isinstance(expected_serial, str)
        or USB_SERIAL.fullmatch(expected_serial) is None
        or expected_serial in reserved
    ):
        raise ValueError("ESP32 USB serial is invalid")

    matches = [
        port
        for port in ports
        if getattr(port, "device", None) == device
        and getattr(port, "serial_number", None) == expected_serial
        and getattr(port, "vid", None) == EXPECTED_VID
    ]
    if len(matches) != 1:
        raise ValueError("expected exactly one verified cat board USB identity")
    return device


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--expected-usb-serial", required=True)
    args = parser.parse_args()
    try:
        from serial.tools import list_ports

        verify_port_identity(
            list_ports.comports(), args.port, args.expected_usb_serial,
            configured_non_cat_serials(),
        )
    except (ImportError, OSError, ValueError):
        print("verified cat board USB identity unavailable", file=sys.stderr)
        return 2
    print("ESP32_PORT_IDENTITY_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
