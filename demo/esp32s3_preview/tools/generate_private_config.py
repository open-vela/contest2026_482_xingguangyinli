#!/usr/bin/env python3
"""Generate the ignored ESP32 network configuration without printing secrets."""

from __future__ import annotations

import argparse
import ast
import os
import pathlib
import ipaddress
import re
import tempfile
from collections.abc import Mapping, Sequence


ROOT = pathlib.Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "generated" / "living_canvas_private_config.h"


def required(name: str, environment: Mapping[str, str]) -> str:
    value = environment.get(name, "")
    if not value:
        raise ValueError(f"missing {name}")
    return value


def c_string(value: str) -> str:
    return (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\r", "\\r")
        .replace("\n", "\\n")
    )


def validate(environment: Mapping[str, str] | None = None) -> dict[str, str]:
    environment = os.environ if environment is None else environment
    device_id = required("LC_CAT_DEVICE_ID", environment)
    if device_id not in ("cat_a", "cat_b"):
        raise ValueError("invalid LC_CAT_DEVICE_ID")

    ssid = required("LC_WIFI_SSID", environment)
    if len(ssid.encode("utf-8")) > 32:
        raise ValueError("invalid LC_WIFI_SSID")

    password = required("LC_WIFI_PASSWORD", environment)
    if (
        not 8 <= len(password) <= 63
        or any(ord(char) < 0x20 or ord(char) > 0x7E for char in password)
    ):
        raise ValueError("invalid LC_WIFI_PASSWORD")

    local_host = required("LC_LOCAL_S1_HOST", environment)
    try:
        local_address = ipaddress.ip_address(local_host)
    except ValueError as error:
        raise ValueError("invalid LC_LOCAL_S1_HOST") from error
    if (
        local_address.version != 4
        or not local_address.is_private
        or local_address.is_loopback
        or local_address.is_link_local
        or local_address.is_multicast
        or local_address.is_unspecified
    ):
        raise ValueError("invalid LC_LOCAL_S1_HOST")

    local_port_text = required("LC_LOCAL_S1_PORT", environment)
    try:
        local_port = int(local_port_text, 10)
    except ValueError as error:
        raise ValueError("invalid LC_LOCAL_S1_PORT") from error
    if str(local_port) != local_port_text or not 1024 <= local_port <= 65535:
        raise ValueError("invalid LC_LOCAL_S1_PORT")

    local_pairing_key_hex = required("LC_LOCAL_PAIRING_KEY_HEX", environment)
    if re.fullmatch(r"[0-9a-f]{64}", local_pairing_key_hex) is None:
        raise ValueError("invalid LC_LOCAL_PAIRING_KEY_HEX")

    return {
        "LC_CAT_DEVICE_ID": device_id,
        "LC_WIFI_SSID": ssid,
        "LC_WIFI_PASSWORD": password,
        "LC_LOCAL_S1_HOST": str(local_address),
        "LC_LOCAL_S1_PORT": str(local_port),
        "LC_LOCAL_PAIRING_KEY_HEX": local_pairing_key_hex,
    }


def output_path() -> pathlib.Path:
    requested = os.environ.get("LC_PRIVATE_CONFIG_OUTPUT", "")
    return pathlib.Path(requested) if requested else OUTPUT


def write_config(values: dict[str, str]) -> pathlib.Path:
    destination = output_path()
    destination.parent.mkdir(parents=True, exist_ok=True)
    lines = ["#pragma once", ""]
    for name, value in values.items():
        if name == "LC_LOCAL_S1_PORT":
            lines.append(f"#define {name} {value}")
        else:
            lines.append(f'#define {name} "{c_string(value)}"')
    lines.append("")
    temporary_path = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            dir=destination.parent,
            prefix=".living_canvas_private_config.",
            delete=False,
        ) as temporary:
            temporary.write("\n".join(lines))
            temporary.flush()
            os.fchmod(temporary.fileno(), 0o600)
            temporary_path = pathlib.Path(temporary.name)
        os.replace(temporary_path, destination)
        os.chmod(destination, 0o600)
    except Exception:
        if temporary_path is not None:
            temporary_path.unlink(missing_ok=True)
        raise
    return destination


def verify_header(path: pathlib.Path, expected_device_id: str) -> None:
    if path.is_symlink() or not path.is_file():
        raise ValueError("private config header is not a regular file")
    lines = path.read_text(encoding="utf-8").splitlines()
    names = (
        "LC_CAT_DEVICE_ID",
        "LC_WIFI_SSID",
        "LC_WIFI_PASSWORD",
        "LC_LOCAL_S1_HOST",
        "LC_LOCAL_S1_PORT",
        "LC_LOCAL_PAIRING_KEY_HEX",
    )
    if len(lines) != 2 + len(names) or lines[:2] != ["#pragma once", ""]:
        raise ValueError("invalid private config header shape")

    parsed: dict[str, str] = {}
    for name, line in zip(names, lines[2:]):
        prefix = f"#define {name} "
        if not line.startswith(prefix):
            raise ValueError("invalid private config header field order")
        raw_value = line[len(prefix):]
        if name == "LC_LOCAL_S1_PORT":
            if re.fullmatch(r"[0-9]+", raw_value) is None:
                raise ValueError("invalid private config header port")
            parsed[name] = raw_value
        else:
            try:
                value = ast.literal_eval(raw_value)
            except (SyntaxError, ValueError) as error:
                raise ValueError("invalid private config header string") from error
            if not isinstance(value, str):
                raise ValueError("invalid private config header string")
            parsed[name] = value

    normalized = validate(parsed)
    if normalized != parsed or parsed["LC_CAT_DEVICE_ID"] != expected_device_id:
        raise ValueError("private config header identity mismatch")


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument("--verify-header", type=pathlib.Path)
    parser.add_argument("--expected-device-id", choices=("cat_a", "cat_b"))
    return parser


def main(argv: Sequence[str] = ()) -> int:
    arguments = _parser().parse_args(argv)
    try:
        if arguments.verify_header is not None:
            if arguments.expected_device_id is None:
                raise ValueError("missing --expected-device-id")
            verify_header(arguments.verify_header, arguments.expected_device_id)
            print("private config verified")
            return 0
        if arguments.expected_device_id is not None:
            raise ValueError("--expected-device-id requires --verify-header")
        destination = write_config(validate())
    except (OSError, UnicodeError, ValueError) as error:
        print(f"private config rejected: {error}", file=os.sys.stderr)
        return 2
    print(f"private config created: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(os.sys.argv[1:]))
