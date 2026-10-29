#!/usr/bin/env python3
"""Derive a private local-backend generation without printing any secret."""

from __future__ import annotations

import argparse
import importlib.util
import os
import pathlib
import secrets
import shutil
import stat
import sys


BACKEND_LAUNCHER = pathlib.Path(__file__).with_name("run_local_roadshow_backend.py")
_SPEC = importlib.util.spec_from_file_location("_living_canvas_local_backend", BACKEND_LAUNCHER)
if _SPEC is None or _SPEC.loader is None:
    raise RuntimeError("backend launcher unavailable")
_BACKEND = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_BACKEND)

MODEL_KEYS = (
    "LIVING_CANVAS_MODEL_BASE_URL",
    "LIVING_CANVAS_MODEL_API_KEY",
    "LIVING_CANVAS_MODEL_NAME",
)
VOICE_KEY = "LIVING_CANVAS_VOICE_API_KEY"
OWNER_KEY = "LIVING_CANVAS_VOICE_DEVICE_TOKEN"
ACCESSORY_KEY = "LIVING_CANVAS_ACCESSORY_DEVICE_TOKEN"


class PreparationError(Exception):
    """Static reason only; never include secret content."""


def _write_private(path: pathlib.Path, content: str) -> None:
    descriptor = os.open(
        path,
        os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_CLOEXEC", 0),
        0o600,
    )
    try:
        with os.fdopen(descriptor, "wb", closefd=True) as stream:
            descriptor = -1
            stream.write(content.encode("ascii"))
            stream.flush()
            os.fsync(stream.fileno())
    finally:
        if descriptor >= 0:
            os.close(descriptor)
    path.chmod(0o600)


def _validate_destination(destination: pathlib.Path) -> pathlib.Path:
    destination = pathlib.Path(destination)
    parent = destination.parent
    if (
        not destination.is_absolute()
        or destination.exists()
        or destination.is_symlink()
        or not parent.is_dir()
        or parent.is_symlink()
        or stat.S_IMODE(parent.stat().st_mode) & 0o077
    ):
        raise PreparationError("invalid_output_directory")
    return destination


def prepare(source_env_file: pathlib.Path,
            destination: pathlib.Path) -> pathlib.Path:
    destination = _validate_destination(destination)
    try:
        source = _BACKEND.load_private_environment_file(source_env_file)
    except _BACKEND.ConfigurationError:
        raise PreparationError("invalid_source_environment") from None
    if any(not source.get(name) for name in MODEL_KEYS):
        raise PreparationError("invalid_source_environment")

    owner_random = secrets.token_urlsafe(32)
    accessory_random = secrets.token_urlsafe(32)
    if owner_random == accessory_random:
        raise PreparationError("token_generation_failed")
    owner = "lc-owner-" + owner_random
    accessory = "lc-accessory-" + accessory_random

    incoming = destination.parent / (
        f".{destination.name}.incoming-{os.getpid()}-{secrets.token_hex(4)}"
    )
    if incoming.exists() or incoming.is_symlink():
        raise PreparationError("invalid_output_directory")
    try:
        incoming.mkdir(mode=0o700)
        incoming.chmod(0o700)
        environment_lines = [f"{name}={source[name]}" for name in MODEL_KEYS]
        if source.get(VOICE_KEY):
            environment_lines.append(f"{VOICE_KEY}={source[VOICE_KEY]}")
        environment_lines.extend((
            f"{OWNER_KEY}={owner}",
            f"{ACCESSORY_KEY}={accessory}",
        ))
        _write_private(incoming / "backend.env", "\n".join(environment_lines) + "\n")
        _write_private(incoming / "owner-token", owner + "\n")
        _write_private(incoming / "accessory-token", accessory + "\n")
        os.replace(incoming, destination)
        destination.chmod(0o700)
    except (OSError, UnicodeError, ValueError):
        shutil.rmtree(incoming, ignore_errors=True)
        raise PreparationError("generation_failed") from None
    return destination


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-env-file", type=pathlib.Path, required=True)
    parser.add_argument("--output-dir", type=pathlib.Path, required=True)
    args = parser.parse_args()
    os.umask(0o077)
    try:
        destination = prepare(args.source_env_file, args.output_dir)
    except PreparationError as error:
        print(f"private runtime generation rejected: {error}", file=sys.stderr)
        return 2
    print(f"private runtime generation ready: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
