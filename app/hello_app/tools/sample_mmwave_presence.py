#!/usr/bin/env python3
"""Read at most three seconds of verified MR60 text without configuring it.

Only validated presence booleans reach stdout. Raw sensor text is neither
printed nor saved. The device is opened read-only and is never reset, flashed,
configured, or written to.
"""

from __future__ import annotations

import argparse
import os
import select
import sys
import time

from esphome_presence_filter import READ_BYTES, filter_stream
from resolve_mmwave_port import (
    EXPECTED_VID,
    configured_mmwave_serial,
    resolve_mmwave_port,
)


OPERATOR_CONFIRMATION = "人在板旁、供电稳定、线缆不动"
SAMPLE_SECONDS = 3.0


class BoundedFdReader:
    def __init__(self, fd, reader, waiter, clock, seconds=SAMPLE_SECONDS):
        self.fd = fd
        self.reader = reader
        self.waiter = waiter
        self.clock = clock
        self.deadline = clock() + seconds

    def read(self, count):
        count = min(max(int(count), 1), READ_BYTES)
        while True:
            remaining = self.deadline - self.clock()
            if remaining <= 0:
                return b""
            ready, _writable, _errors = self.waiter(
                [self.fd], [], [], min(remaining, 0.25)
            )
            if not ready:
                continue
            try:
                return self.reader(self.fd, count)
            except BlockingIOError:
                continue


def sample_presence(
    ports,
    operator_confirmation,
    output_stream,
    error_stream,
    *,
    expected_serial,
    opener=os.open,
    closer=os.close,
    reader=os.read,
    waiter=select.select,
    clock=time.monotonic,
):
    if operator_confirmation != OPERATOR_CONFIRMATION:
        raise ValueError("fresh operator confirmation required")
    device = resolve_mmwave_port(ports, expected_serial)
    flags = os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK
    fd = opener(device, flags)
    try:
        bounded = BoundedFdReader(fd, reader, waiter, clock)
        return filter_stream(bounded, output_stream, error_stream)
    finally:
        closer(fd)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--execute", action="store_true")
    parser.add_argument("--operator-confirmation", default="")
    args = parser.parse_args()
    if not args.execute:
        print("dry run: verified read-only mmWave sample is ready")
        return 0
    try:
        from serial.tools import list_ports

        return sample_presence(
            list_ports.comports(),
            args.operator_confirmation,
            sys.stdout,
            sys.stderr,
            expected_serial=configured_mmwave_serial(),
        )
    except (ImportError, OSError, ValueError):
        print("verified read-only mmWave sample unavailable", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
