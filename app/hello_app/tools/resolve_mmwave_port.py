#!/usr/bin/env python3
"""Resolve a privately configured MR60 USB identity to one callout path.

This tool only enumerates host serial metadata.  It never opens, configures,
resets, flashes, reads from, or writes to the selected device.
"""

from __future__ import annotations

import re
import os
import sys
from typing import Iterable


EXPECTED_VID = 0x303A
CALLOUT_PATH = re.compile(r"^/dev/cu\.[A-Za-z0-9._-]{1,128}$")
USB_SERIAL = re.compile(r"^[A-Za-z0-9:._-]{1,64}$")


def configured_mmwave_serial(environ=None) -> str:
    source = os.environ if environ is None else environ
    value = source.get("LIVING_CANVAS_MMWAVE_USB_SERIAL", "")
    if not isinstance(value, str) or USB_SERIAL.fullmatch(value) is None:
        raise ValueError("verified mmWave identity is not configured")
    return value


def resolve_mmwave_port(ports: Iterable[object], expected_serial: str) -> str:
    """Return one exact MR60 callout path or fail closed."""
    if (not isinstance(expected_serial, str) or
            USB_SERIAL.fullmatch(expected_serial) is None):
        raise ValueError("verified mmWave identity is invalid")
    matches = []
    for port in ports:
        if getattr(port, "serial_number", None) != expected_serial:
            continue
        if getattr(port, "vid", None) != EXPECTED_VID:
            continue
        device = getattr(port, "device", None)
        if not isinstance(device, str) or CALLOUT_PATH.fullmatch(device) is None:
            continue
        matches.append(device)
    if len(matches) != 1:
        raise ValueError("expected exactly one verified mmWave callout port")
    return matches[0]


def main() -> int:
    if len(sys.argv) != 1:
        print("mmWave port resolver takes no arguments", file=sys.stderr)
        return 64
    try:
        from serial.tools import list_ports

        device = resolve_mmwave_port(
            list_ports.comports(), configured_mmwave_serial()
        )
    except (ImportError, OSError, ValueError):
        print("verified mmWave callout port unavailable", file=sys.stderr)
        return 2
    print(device)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
