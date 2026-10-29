#!/usr/bin/env python3
"""Create matched private configuration for cat A, cat B and Gemini-S1."""

from __future__ import annotations

import datetime
import os
import pathlib
import re
import secrets
import shutil
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUNDLE_ROOT = ROOT / "build" / "pairing"
PRIVATE_CONFIG_GENERATOR = ROOT / "tools" / "generate_private_config.py"
KEY_PATTERN = re.compile(r"[0-9a-f]{64}")


def _pairing_keys() -> tuple[str, str]:
    cat_a = os.environ.get("LC_CAT_A_PAIRING_KEY_HEX", "")
    cat_b = os.environ.get("LC_CAT_B_PAIRING_KEY_HEX", "")
    if bool(cat_a) != bool(cat_b):
        raise ValueError("both cat pairing keys must be supplied together")
    if not cat_a:
        cat_a = secrets.token_hex(32)
        cat_b = secrets.token_hex(32)
    if KEY_PATTERN.fullmatch(cat_a) is None or KEY_PATTERN.fullmatch(cat_b) is None:
        raise ValueError("cat pairing keys must be 64 lowercase hex characters")
    if secrets.compare_digest(cat_a, cat_b):
        raise ValueError("cat pairing keys must be independent")
    return cat_a, cat_b


def _generator_environment(device_id: str, key: str,
                           destination: pathlib.Path) -> dict[str, str]:
    environment = {
        "LC_CAT_DEVICE_ID": device_id,
        "LC_LOCAL_PAIRING_KEY_HEX": key,
        "LC_PRIVATE_CONFIG_OUTPUT": str(destination),
        "PYTHONDONTWRITEBYTECODE": "1",
    }
    for name in (
        "LC_WIFI_SSID",
        "LC_WIFI_PASSWORD",
        "LC_LOCAL_S1_HOST",
        "LC_LOCAL_S1_PORT",
    ):
        value = os.environ.get(name, "")
        if not value:
            raise ValueError(f"missing {name}")
        environment[name] = value
    return environment


def _generate_header(device_id: str, key: str,
                     destination: pathlib.Path) -> None:
    result = subprocess.run(
        [sys.executable, str(PRIVATE_CONFIG_GENERATOR)],
        env=_generator_environment(device_id, key, destination),
        check=False,
        capture_output=True,
        text=True,
        timeout=15,
    )
    if result.returncode != 0 or not destination.is_file():
        raise ValueError(f"private config generation failed for {device_id}")


def _write_s1_keys(path: pathlib.Path, cat_a: str, cat_b: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    path.write_text(f"cat_a={cat_a}\ncat_b={cat_b}\n", encoding="ascii")
    path.chmod(0o600)


def generate_bundle() -> pathlib.Path:
    cat_a, cat_b = _pairing_keys()
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    destination = BUNDLE_ROOT / f"pair-{stamp}-{os.getpid()}"
    incoming = BUNDLE_ROOT / f".incoming-{stamp}-{os.getpid()}"
    if destination.exists() or incoming.exists():
        raise OSError("pairing bundle destination already exists")

    BUNDLE_ROOT.mkdir(parents=True, exist_ok=True, mode=0o700)
    BUNDLE_ROOT.chmod(0o700)
    incoming.mkdir(mode=0o700)
    try:
        _generate_header(
            "cat_a", cat_a,
            incoming / "cat_a" / "living_canvas_private_config.h",
        )
        _generate_header(
            "cat_b", cat_b,
            incoming / "cat_b" / "living_canvas_private_config.h",
        )
        _write_s1_keys(incoming / "s1" / "local_accessory_keys", cat_a, cat_b)
        for directory in (incoming, incoming / "cat_a", incoming / "cat_b",
                          incoming / "s1"):
            directory.chmod(0o700)
        os.replace(incoming, destination)
        destination.chmod(0o700)
    except Exception:
        shutil.rmtree(incoming, ignore_errors=True)
        raise
    return destination


def main() -> int:
    try:
        destination = generate_bundle()
    except (OSError, subprocess.SubprocessError, UnicodeError, ValueError) as error:
        print(f"pairing bundle rejected: {error}", file=sys.stderr)
        return 2
    print(f"private pairing bundle created: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
