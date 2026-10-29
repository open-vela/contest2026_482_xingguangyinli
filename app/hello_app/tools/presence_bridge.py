#!/usr/bin/env python3
"""Translate privacy-minimal JSON presence observations into LC1 events.

The vendor-specific process stays outside this script. It writes one JSON
object per line to stdin, for example {"detected": true, "confidence": 82}.
This bridge never accepts frames, images, identities, faces, or embeddings.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Any, Dict, TextIO


WIRE_ID = re.compile(r"^[A-Za-z0-9_-]{1,23}$")
PRIVATE_FIELDS = {"image", "frame", "face", "identity", "embedding"}
ALLOWED_FIELDS = {"detected", "confidence"}


def _wire_id(value: str, name: str) -> str:
    if not isinstance(value, str) or WIRE_ID.fullmatch(value) is None:
        raise ValueError(f"{name} must use 1-23 wire-safe characters")
    return value


class PresenceBridge:
    def __init__(self, source: str, device_id: str, session_id: str) -> None:
        if source not in {"accessory", "mmwave", "camera"}:
            raise ValueError("source must be accessory, mmwave, or camera")
        self.source = source
        self.device_id = _wire_id(device_id, "device_id")
        self.session_id = _wire_id(session_id, "session_id")
        self.sequence = 0

    def encode(self, observation: Dict[str, Any]) -> str:
        if not isinstance(observation, dict):
            raise ValueError("observation must be a JSON object")

        private = PRIVATE_FIELDS.intersection(observation)
        if private:
            raise ValueError("privacy-sensitive fields are not accepted")
        unsupported = set(observation).difference(ALLOWED_FIELDS)
        if unsupported:
            raise ValueError("observation contains an unsupported field")

        detected = observation.get("detected")
        if type(detected) is not bool:
            raise ValueError("detected must be true or false")

        if self.source == "camera":
            confidence = observation.get("confidence", 0 if not detected else None)
            if type(confidence) is not int or not 0 <= confidence <= 100:
                raise ValueError("confidence must be an integer from 0 to 100")
            event_type = "presence_confidence"
            value = confidence if detected else 0
        else:
            if "confidence" in observation:
                raise ValueError("confidence is only valid for camera observations")
            event_type = "presence"
            value = 1 if detected else 0

        if self.sequence >= 0xFFFFFFFF:
            raise ValueError("LC1 sequence exhausted; start a new session")
        self.sequence += 1
        return (
            f"LC1,{self.device_id},{self.session_id},{self.sequence},"
            f"{event_type},{value}\n"
        )


def _arguments(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert JSON-line presence observations to bounded LC1 events"
    )
    parser.add_argument("--source", choices=("accessory", "mmwave", "camera"), required=True)
    parser.add_argument("--device-id")
    parser.add_argument("--session-id", default=f"bridge_{os.getpid()}")
    parser.add_argument(
        "--output",
        default="-",
        help="confirmed event path, or - for stdout (default)",
    )
    return parser.parse_args(argv)


def _open_output(path: str) -> tuple[TextIO, bool]:
    if path == "-":
        return sys.stdout, False
    return Path(path).open("w", encoding="ascii", buffering=1), True


def main(argv: list[str] | None = None) -> int:
    args = _arguments(sys.argv[1:] if argv is None else argv)
    device_id = args.device_id or f"{args.source}_bridge"
    try:
        bridge = PresenceBridge(args.source, device_id, args.session_id)
        output, should_close = _open_output(args.output)
    except (OSError, ValueError) as error:
        print(f"presence bridge setup rejected: {error}", file=sys.stderr)
        return 2

    rejected = False
    try:
        for line_number, line in enumerate(sys.stdin, start=1):
            if not line.strip():
                continue
            try:
                observation = json.loads(line)
                output.write(bridge.encode(observation))
                output.flush()
            except (TypeError, ValueError, json.JSONDecodeError) as error:
                rejected = True
                print(
                    f"presence bridge line {line_number} rejected: {error}",
                    file=sys.stderr,
                )
    finally:
        if should_close:
            output.close()

    return 2 if rejected else 0


if __name__ == "__main__":
    raise SystemExit(main())
