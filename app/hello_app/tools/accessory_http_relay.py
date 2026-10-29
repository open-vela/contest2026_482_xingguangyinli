#!/usr/bin/env python3
"""Forward privacy-minimal presence booleans to the accessory relay.

This process only reads validated JSON lines from stdin. It does not open,
configure, reset, or flash a sensor. The endpoint and bearer token come from
the environment so secrets do not appear in command-line process listings.
"""

from __future__ import annotations

import json
import os
import re
import secrets
import sys
import urllib.error
import urllib.parse
import urllib.request
from typing import BinaryIO, Callable, TextIO


MAX_INPUT_BYTES = 512
MAX_RESPONSE_BYTES = 2048
REQUEST_TIMEOUT_SECONDS = 5
MAX_ATTEMPTS = 3
SESSION_ID = re.compile(r"^[A-Za-z0-9._-]{1,23}$")
LOOPBACK_NAMES = {"127.0.0.1", "localhost", "::1"}


def _validate_endpoint(value: str) -> str:
    if not isinstance(value, str):
        raise ValueError("accessory endpoint is invalid")
    parsed = urllib.parse.urlsplit(value)
    if parsed.username or parsed.password or parsed.query or parsed.fragment:
        raise ValueError("accessory endpoint is invalid")
    if parsed.path != "/v1/accessories/events":
        raise ValueError("accessory endpoint is invalid")
    if parsed.scheme == "https" and parsed.hostname:
        return value
    if (parsed.scheme == "http" and parsed.hostname in LOOPBACK_NAMES):
        return value
    raise ValueError("accessory endpoint must use HTTPS")


def _validate_token(value: str) -> str:
    if not isinstance(value, str) or not 32 <= len(value) <= 512:
        raise ValueError("accessory token is invalid")
    if any(ord(character) < 33 or ord(character) > 126 for character in value):
        raise ValueError("accessory token is invalid")
    return value


class AccessoryHttpRelay:
    def __init__(
        self,
        endpoint: str,
        token: str,
        session_id: str,
        opener: Callable = urllib.request.urlopen,
    ) -> None:
        self.endpoint = _validate_endpoint(endpoint)
        self.token = _validate_token(token)
        if not isinstance(session_id, str) or SESSION_ID.fullmatch(session_id) is None:
            raise ValueError("accessory session is invalid")
        self.session_id = session_id
        self.opener = opener
        self.sequence = 1

    def encode(self, observation: dict) -> dict:
        if not isinstance(observation, dict) or set(observation) != {"detected"}:
            raise ValueError("presence observation is invalid")
        if type(observation["detected"]) is not bool:
            raise ValueError("presence observation is invalid")
        return {
            "device_id": "mmwave_bridge",
            "session_id": self.session_id,
            "sequence": self.sequence,
            "event": "presence",
            "value": observation["detected"],
        }

    @staticmethod
    def _is_replay_acknowledgement(error: urllib.error.HTTPError) -> bool:
        if error.code != 409:
            return False
        try:
            body = error.read(MAX_RESPONSE_BYTES + 1)
            if len(body) > MAX_RESPONSE_BYTES:
                return False
            decoded = json.loads(body)
            return decoded == {"detail": "accessory_event_replayed"}
        except (OSError, UnicodeError, ValueError, json.JSONDecodeError):
            return False

    def _accepted_response(self, response, expected: dict) -> bool:
        if getattr(response, "status", None) != 202:
            return False
        try:
            body = response.read(MAX_RESPONSE_BYTES + 1)
            if len(body) > MAX_RESPONSE_BYTES:
                return False
            decoded = json.loads(body)
        except (OSError, UnicodeError, ValueError, json.JSONDecodeError):
            return False
        return (
            isinstance(decoded, dict)
            and type(decoded.get("cursor")) is int
            and decoded.get("cursor", 0) >= 1
            and all(decoded.get(key) == value for key, value in expected.items())
        )

    def deliver(self, observation: dict) -> bool:
        expected = self.encode(observation)
        body = json.dumps(expected, separators=(",", ":")).encode("ascii")
        for _attempt in range(MAX_ATTEMPTS):
            request = urllib.request.Request(
                self.endpoint,
                data=body,
                method="POST",
                headers={
                    "Authorization": "Bearer " + self.token,
                    "Content-Type": "application/json",
                },
            )
            try:
                with self.opener(request, timeout=REQUEST_TIMEOUT_SECONDS) as response:
                    if self._accepted_response(response, expected):
                        self.sequence += 1
                        return True
            except urllib.error.HTTPError as error:
                if self._is_replay_acknowledgement(error):
                    self.sequence += 1
                    return True
                if error.code in (400, 401, 403, 409, 422):
                    return False
            except (OSError, TimeoutError, urllib.error.URLError):
                pass
        return False


def relay_stream(input_stream: BinaryIO, relay, error_stream: TextIO) -> int:
    while True:
        try:
            line = input_stream.readline(MAX_INPUT_BYTES + 1)
        except OSError:
            error_stream.write("presence input unavailable\n")
            return 1
        if not line:
            return 0
        if len(line) > MAX_INPUT_BYTES or not line.endswith(b"\n"):
            error_stream.write("presence input rejected\n")
            return 2
        try:
            observation = json.loads(line)
            if not relay.deliver(observation):
                error_stream.write("presence delivery unavailable\n")
                return 1
        except (UnicodeError, ValueError, json.JSONDecodeError):
            error_stream.write("presence input rejected\n")
            return 2


def main() -> int:
    if len(sys.argv) != 1:
        print("accessory relay takes configuration from the environment", file=sys.stderr)
        return 64
    endpoint = os.environ.get("LC_ACCESSORY_URL", "")
    token = os.environ.get("LC_ACCESSORY_DEVICE_TOKEN", "")
    session_id = os.environ.get(
        "LC_ACCESSORY_SESSION_ID", "mw_" + secrets.token_hex(8)
    )
    try:
        relay = AccessoryHttpRelay(endpoint, token, session_id)
    except ValueError:
        print("accessory relay setup rejected", file=sys.stderr)
        return 2
    return relay_stream(sys.stdin.buffer, relay, sys.stderr)


if __name__ == "__main__":
    raise SystemExit(main())
