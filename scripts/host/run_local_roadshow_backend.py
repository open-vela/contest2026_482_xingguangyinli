#!/usr/bin/env python3
"""Run the real Living Canvas backend on one explicit RFC1918 interface.

Secrets are read only from the inherited environment, except that the
owner-device token may come from one explicit locked-down file named by the
environment. This launcher never accepts secrets as command arguments or
prints their values. It is an optional roadshow fallback for a private,
trusted LAN; public deployment remains HTTPS.
"""

import argparse
import ipaddress
import json
import os
import pathlib
import re
import stat
import sys
import urllib.parse


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
BACKEND_SOURCE = REPO_ROOT / "backend" / "src"
RFC1918 = tuple(ipaddress.ip_network(value) for value in (
    "10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"
))
MIMO_BASE_URL = "https://api.xiaomimimo.com/v1"
DASHSCOPE_BASE_URL = "https://dashscope.aliyuncs.com/compatible-mode/v1"
SUPPORTED_MIMO_MODELS = frozenset(("mimo-v2.6-flash", "mimo-v2.6-pro"))
SUPPORTED_DASHSCOPE_MODELS = frozenset(("qwen-flash",))
TOKEN_PLAN_KEY_PREFIXES = ("tp-", "ttp-")
MODEL_REQUIRED = (
    "LIVING_CANVAS_MODEL_BASE_URL",
    "LIVING_CANVAS_MODEL_API_KEY",
    "LIVING_CANVAS_MODEL_NAME",
)
DEVICE_TOKEN = "LIVING_CANVAS_VOICE_DEVICE_TOKEN"
DEVICE_TOKEN_FILE = "LIVING_CANVAS_VOICE_DEVICE_TOKEN_FILE"
ACCESSORY_TOKEN = "LIVING_CANVAS_ACCESSORY_DEVICE_TOKEN"
ENVIRONMENT_FILE_KEYS = frozenset(MODEL_REQUIRED + (
    "LIVING_CANVAS_VOICE_API_KEY",
    DEVICE_TOKEN,
    ACCESSORY_TOKEN,
    "LIVING_CANVAS_HOST",
    "LIVING_CANVAS_PORT",
    "LIVING_CANVAS_PUBLIC_BASE_URL",
    "LIVING_CANVAS_DATA_DIR",
))


class ConfigurationError(Exception):
    """Static safe reason only; never include a secret value."""


def _private_ipv4(value):
    try:
        address = ipaddress.ip_address(value)
    except ValueError:
        raise ConfigurationError("invalid_private_host") from None
    if address.version != 4 or not any(address in network for network in RFC1918):
        raise ConfigurationError("invalid_private_host")
    return str(address)


def _secret(value):
    return (isinstance(value, str) and 8 <= len(value) <= 512 and
            all(0x21 <= ord(character) <= 0x7E for character in value))


def _model_url(value):
    if not isinstance(value, str) or any(ord(c) <= 0x20 for c in value):
        return False
    try:
        parsed = urllib.parse.urlsplit(value)
        port = parsed.port
    except ValueError:
        return False
    return (value.rstrip("/") in (MIMO_BASE_URL, DASHSCOPE_BASE_URL) and
            parsed.scheme == "https" and bool(parsed.hostname) and
            parsed.username is None and parsed.password is None and
            not parsed.query and not parsed.fragment and
            (port is None or 1 <= port <= 65535))


def _prepare_data_directory(value):
    candidate = pathlib.Path(value).expanduser()
    if candidate.is_symlink():
        raise ConfigurationError("invalid_data_dir")
    path = candidate.resolve()
    if path.exists():
        if not path.is_dir() or path.is_symlink():
            raise ConfigurationError("invalid_data_dir")
        if stat.S_IMODE(path.stat().st_mode) & 0o077:
            raise ConfigurationError("insecure_data_dir")
    else:
        try:
            path.mkdir(mode=0o700, parents=False)
        except OSError:
            raise ConfigurationError("invalid_data_dir") from None
    return path


def _read_private_device_token(value):
    if not isinstance(value, str) or not value:
        raise ConfigurationError("invalid_device_token_file")
    path = pathlib.Path(value)
    if not path.is_absolute():
        raise ConfigurationError("invalid_device_token_file")
    descriptor = -1
    try:
        flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | \
            getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(path, flags)
        metadata = os.fstat(descriptor)
        if (not stat.S_ISREG(metadata.st_mode) or
                stat.S_IMODE(metadata.st_mode) & 0o077 or
                metadata.st_size <= 0 or metadata.st_size > 130):
            raise ConfigurationError("invalid_device_token_file")
        with os.fdopen(descriptor, "rb", closefd=True) as stream:
            descriptor = -1
            raw = stream.read(131)
    except ConfigurationError:
        raise
    except OSError:
        raise ConfigurationError("invalid_device_token_file") from None
    finally:
        if descriptor >= 0:
            os.close(descriptor)
    if len(raw) > 130:
        raise ConfigurationError("invalid_device_token_file")
    if raw.endswith(b"\r\n"):
        raw = raw[:-2]
    elif raw.endswith(b"\n"):
        raw = raw[:-1]
    if b"\r" in raw or b"\n" in raw:
        raise ConfigurationError("invalid_device_token_file")
    try:
        return raw.decode("ascii")
    except UnicodeDecodeError:
        raise ConfigurationError("invalid_device_token_file") from None


def load_private_environment_file(value):
    path = pathlib.Path(value)
    if not path.is_absolute():
        raise ConfigurationError("invalid_environment_file")
    descriptor = -1
    try:
        flags = os.O_RDONLY | getattr(os, "O_CLOEXEC", 0) | \
            getattr(os, "O_NOFOLLOW", 0)
        descriptor = os.open(path, flags)
        metadata = os.fstat(descriptor)
        if (not stat.S_ISREG(metadata.st_mode) or
                stat.S_IMODE(metadata.st_mode) != 0o600 or
                metadata.st_size <= 0 or metadata.st_size > 16384):
            raise ConfigurationError("invalid_environment_file")
        with os.fdopen(descriptor, "rb", closefd=True) as stream:
            descriptor = -1
            raw = stream.read(16385)
    except ConfigurationError:
        raise
    except OSError:
        raise ConfigurationError("invalid_environment_file") from None
    finally:
        if descriptor >= 0:
            os.close(descriptor)
    if len(raw) > 16384 or b"\r" in raw or b"\x00" in raw:
        raise ConfigurationError("invalid_environment_file")
    try:
        content = raw.decode("ascii")
    except UnicodeDecodeError:
        raise ConfigurationError("invalid_environment_file") from None
    loaded = {}
    for line in content.split("\n"):
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            raise ConfigurationError("invalid_environment_file")
        key, value = line.split("=", 1)
        if (key not in ENVIRONMENT_FILE_KEYS or key in loaded or
                value != value.strip() or
                any(ord(character) < 0x21 or ord(character) > 0x7E
                    for character in value)):
            # Empty optional values are allowed; all non-empty values must be
            # single-line printable ASCII without shell syntax interpretation.
            if value:
                raise ConfigurationError("invalid_environment_file")
            if key not in ENVIRONMENT_FILE_KEYS or key in loaded:
                raise ConfigurationError("invalid_environment_file")
        loaded[key] = value
    if not loaded:
        raise ConfigurationError("invalid_environment_file")
    return loaded


def merge_private_environment(environ, path):
    merged = dict(environ)
    for key, value in load_private_environment_file(path).items():
        existing = merged.get(key, "")
        if existing and value and existing != value:
            raise ConfigurationError("ambiguous_environment")
        if not existing:
            merged[key] = value
    return merged


def _device_token(environ):
    direct = environ.get(DEVICE_TOKEN, "")
    token_file = environ.get(DEVICE_TOKEN_FILE, "")
    if direct and token_file:
        raise ConfigurationError("ambiguous_device_token")
    if direct:
        return direct
    if token_file:
        return _read_private_device_token(token_file)
    raise ConfigurationError("configuration_missing")


def configure(host, port, data_dir, environ):
    host = _private_ipv4(host)
    if not isinstance(port, int) or not 1024 <= port <= 65535:
        raise ConfigurationError("invalid_port")
    if (any(not environ.get(name) for name in MODEL_REQUIRED) or
            not environ.get(ACCESSORY_TOKEN)):
        raise ConfigurationError("configuration_missing")
    device_token = _device_token(environ)
    accessory_token = environ[ACCESSORY_TOKEN]
    if not _model_url(environ["LIVING_CANVAS_MODEL_BASE_URL"]):
        raise ConfigurationError("invalid_model_url")
    model_base_url = environ["LIVING_CANVAS_MODEL_BASE_URL"].rstrip("/")
    model_api_key = environ["LIVING_CANVAS_MODEL_API_KEY"]
    voice_api_key = environ.get("LIVING_CANVAS_VOICE_API_KEY", "")
    if not voice_api_key and model_base_url == DASHSCOPE_BASE_URL:
        # One DashScope provider credential legitimately covers qwen-flash,
        # qwen3-asr-flash and qwen3-tts-flash. Device credentials remain
        # separate and are still rejected if they reuse this value.
        voice_api_key = model_api_key
    if not voice_api_key:
        raise ConfigurationError("configuration_missing")
    if environ["LIVING_CANVAS_MODEL_API_KEY"].startswith(
            TOKEN_PLAN_KEY_PREFIXES):
        raise ConfigurationError(
            "token_plan_not_permitted_for_product_backend")
    if not _secret(model_api_key) or not _secret(voice_api_key):
        raise ConfigurationError("invalid_provider_configuration")
    model_name = environ["LIVING_CANVAS_MODEL_NAME"]
    supported_models = {
        MIMO_BASE_URL: SUPPORTED_MIMO_MODELS,
        DASHSCOPE_BASE_URL: SUPPORTED_DASHSCOPE_MODELS,
    }[model_base_url]
    if model_name not in supported_models:
        raise ConfigurationError("invalid_model_name")
    if (not isinstance(device_token, str) or
        not device_token.startswith("lc-owner-") or
        re.fullmatch(r"[-A-Za-z0-9._]{32,128}", device_token) is None or
        device_token in (model_api_key, voice_api_key)):
        raise ConfigurationError("invalid_device_token")
    if (not isinstance(accessory_token, str) or
            accessory_token.startswith("sk-") or
            re.fullmatch(r"[-A-Za-z0-9._]{32,128}", accessory_token) is None or
            accessory_token in (
                model_api_key,
                voice_api_key,
                device_token,
            )):
        raise ConfigurationError("invalid_accessory_token")

    data_path = _prepare_data_directory(data_dir)
    configured = dict(environ)
    configured.pop(DEVICE_TOKEN_FILE, None)
    configured.update({
        "LIVING_CANVAS_HOST": host,
        "LIVING_CANVAS_PORT": str(port),
        "LIVING_CANVAS_PUBLIC_BASE_URL": f"http://{host}:{port}",
        "LIVING_CANVAS_DATA_DIR": str(data_path),
        DEVICE_TOKEN: device_token,
        "LIVING_CANVAS_VOICE_API_KEY": voice_api_key,
    })
    return configured


def start_server(configured):
    # Import after validation and environment installation: app's module-level
    # factory reads configuration exactly once.
    os.environ.update(configured)
    import uvicorn
    uvicorn.run(
        "living_canvas_backend.app:app",
        app_dir=str(BACKEND_SOURCE),
        host=configured["LIVING_CANVAS_HOST"],
        port=int(configured["LIVING_CANVAS_PORT"]),
        access_log=False,
        proxy_headers=False,
        log_level="warning",
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True,
                        help="one RFC1918 IPv4 address assigned to this computer")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--data-dir", type=pathlib.Path, required=True)
    parser.add_argument("--env-file", type=pathlib.Path,
                        help="absolute 0600 KEY=VALUE file; values are never printed")
    parser.add_argument("--check", action="store_true",
                        help="validate configuration without opening a socket")
    args = parser.parse_args()
    os.umask(0o077)
    try:
        environment = (merge_private_environment(os.environ, args.env_file)
                       if args.env_file else os.environ)
        configured = configure(args.host, args.port, args.data_dir, environment)
    except ConfigurationError as error:
        print(json.dumps({"status": "FAIL", "reason": str(error)},
                         sort_keys=True))
        return 1
    if args.check:
        print(json.dumps({"status": "PASS", "ai": "CONFIGURED",
                          "voice": "CONFIGURED",
                          "accessories": "CONFIGURED",
                          "server": "NOT_STARTED"},
                         sort_keys=True))
        return 0
    start_server(configured)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
