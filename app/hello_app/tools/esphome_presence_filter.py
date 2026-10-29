#!/usr/bin/env python3
"""Filter a trusted MR60BHA2 ESPHome text stream to presence booleans.

Stdin only: this does not open serial, configure UART, reset, flash, or upload.
Unlike the legacy bridge it does not parse or cache vital measurements.
Unknown/malformed/silent input is not absence. This host bench adapter is not
a standalone S1 transport or authenticated sensor protocol. Live callers must
verify device identity, avoid replaying saved logs, and retain time/state gates.
"""
from __future__ import annotations

import re
import sys

MAX_LINE_BYTES = 512
READ_BYTES = 128
COLOR = re.compile(rb"\x1b\[[0-9;]{0,16}m")
TARGET = re.compile(
    rb"(?:\[\d{2}:\d{2}:\d{2}(?:\.\d{1,6})?\])?"
    rb"\[D\]\[sensor:\d{1,4}\]:[ \t]*'Target number':[ \t]+Sending state[ \t]+"
    rb"(?P<count>\d{1,3})(?:\.0{1,5})?[ \t]+with[ \t]+[0-5][ \t]+decimals of accuracy",
    re.IGNORECASE,
)


def parse_presence_line(line: bytes) -> bool | None:
    """An explicit complete sensor state, never any other numerical reading."""
    if not isinstance(line, bytes) or len(line) > MAX_LINE_BYTES:
        return None
    # Only terminal color codes and CR are presentation; other bytes stay intact.
    cleaned = COLOR.sub(b"", line.removesuffix(b"\r"))
    match = TARGET.fullmatch(cleaned)
    if match is None:
        return None
    count = int(match.group("count"))
    return count > 0 if count <= 255 else None


def filter_stream(input_stream, output_stream, error_stream) -> int:
    buffer = bytearray()
    oversized = False
    accepted = False
    try:
        while True:
            try:
                chunk = input_stream.read(READ_BYTES)
            except OSError:
                error_stream.write("presence input unavailable\n")
                return 1
            if not chunk:
                break
            for value in chunk:
                if value == 10:
                    observation = None if oversized else parse_presence_line(bytes(buffer))
                    buffer[:] = b"\0" * len(buffer)
                    buffer.clear()
                    oversized = False
                    if observation is not None:
                        try:
                            output_stream.write('{"detected":' +
                                ("true" if observation else "false") + '}\n')
                            output_stream.flush()
                        except OSError:
                            error_stream.write("presence output unavailable\n")
                            return 1
                        accepted = True
                elif not oversized:
                    if len(buffer) == MAX_LINE_BYTES:
                        buffer[:] = b"\0" * len(buffer)
                        buffer.clear()
                        oversized = True
                    else:
                        buffer.append(value)
        # Unterminated final bytes are not a complete observation.
        if not accepted:
            error_stream.write("no validated presence report\n")
            return 2
        return 0
    finally:
        buffer[:] = b"\0" * len(buffer)
        buffer.clear()


def main() -> int:
    if len(sys.argv) != 1:
        print("stdin-only presence filter takes no arguments", file=sys.stderr)
        return 64
    return filter_stream(sys.stdin.buffer, sys.stdout, sys.stderr)


if __name__ == "__main__":
    raise SystemExit(main())
