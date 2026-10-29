#!/usr/bin/env python3
"""Run every host-only roadshow software gate without touching hardware."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
from typing import NamedTuple, Sequence


class ConfigurationError(ValueError):
    """The requested gate plan is incomplete or points outside the repo."""


class Gate(NamedTuple):
    name: str
    command: tuple[str, ...]
    environment: dict[str, str]


def _bounded_timeout(value: str) -> int:
    try:
        seconds = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("timeout must be an integer") from error
    if not 1 <= seconds <= 3600:
        raise argparse.ArgumentTypeError("timeout must be between 1 and 3600 seconds")
    return seconds


def _require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise ConfigurationError(f"missing_{label}={path}")


def _is_sensitive_environment_name(name: str) -> bool:
    upper = name.upper()
    if upper.startswith("LIVING_CANVAS_"):
        return True
    if upper in {
        "ALL_PROXY", "CADDY_BINARY", "CADDYFILE_UNDER_TEST",
        "HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY", "SSH_AUTH_SOCK",
    }:
        return True
    if upper.startswith((
        "ANTHROPIC_", "AWS_", "AZURE_", "GH_", "GITHUB_", "GITLAB_",
        "GOOGLE_", "OPENAI_", "SLACK_",
    )):
        return True
    return upper.endswith((
        "_API_KEY", "_CREDENTIAL", "_CREDENTIALS", "_PASSWORD", "_SECRET",
        "_TOKEN",
    ))


def build_plan(repo: Path, python: str, cjson_dir: Path,
               esp_python: str | None = None) -> tuple[Gate, ...]:
    repo = repo.resolve()
    cjson_dir = cjson_dir.resolve()
    if not repo.is_dir():
        raise ConfigurationError(f"invalid_repo={repo}")
    _require_file(repo / "backend" / "pyproject.toml", "backend")
    _require_file(repo / "app" / "hello_app" / "tests" / "host" /
                  "Makefile", "s1_tests")
    _require_file(repo / "demo" / "esp32s3_preview" / "tests" /
                  "run_tests.sh", "esp32_tests")
    _require_file(repo / "scripts" / "device" / "tests" /
                  "test_apply_gemini_roadshow_config.py",
                  "device_host_tests")
    _require_file(repo / "scripts" / "device" / "tests" /
                  "test_evidence_privacy.sh", "evidence_privacy_test")
    _require_file(repo / "tests" / "test_no_legacy_brand.sh",
                  "brand_clean_test")
    _require_file(cjson_dir / "cJSON.c", "cjson_source")
    _require_file(cjson_dir / "cJSON.h", "cjson_header")
    if not python:
        raise ConfigurationError("invalid_python")
    if esp_python is None:
        esp_python = python
    if not esp_python:
        raise ConfigurationError("invalid_esp_python")

    common = {
        name: value for name, value in os.environ.items()
        if not _is_sensitive_environment_name(name)
    }
    common["PYTHONDONTWRITEBYTECODE"] = "1"
    backend_environment = dict(common)
    backend_environment["PYTHONPATH"] = str(repo / "backend" / "src")
    esp_environment = dict(common)
    esp_environment["PYTHON_BIN"] = esp_python

    return (
        Gate(
            "backend",
            (python, "-m", "pytest", "-q", "backend/tests",
             "scripts/host/tests"),
            backend_environment,
        ),
        Gate(
            "device-host",
            (python, "-m", "pytest", "-q", "scripts/device/tests"),
            common,
        ),
        Gate(
            "s1-host",
            ("make", "-B", "-C", "app/hello_app/tests/host", "test",
             f"CJSON_DIR={cjson_dir}"),
            common,
        ),
        Gate(
            "esp32-host",
            ("sh", "demo/esp32s3_preview/tests/run_tests.sh"),
            esp_environment,
        ),
        Gate(
            "evidence-privacy",
            ("bash", "scripts/device/tests/test_evidence_privacy.sh"),
            common,
        ),
        Gate(
            "brand-clean",
            ("bash", "tests/test_no_legacy_brand.sh"),
            common,
        ),
        Gate("diff-check", ("git", "diff", "--check"), common),
    )


def _parser() -> argparse.ArgumentParser:
    default_repo = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(
        description=(
            "Run backend, device tools, S1 host, ESP32 host, evidence "
            "privacy, branding "
            "and diff gates. This command "
            "does not inspect, flash, restart or connect to physical devices."
        )
    )
    parser.add_argument("--repo", type=Path, default=default_repo)
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument(
        "--esp-python", default=os.environ.get("ESP_PYTHON_BIN"),
        help="Python with Pillow for the ESP32 asset tests (default: --python)",
    )
    parser.add_argument(
        "--cjson-dir", type=Path,
        default=Path(os.environ.get("CJSON_DIR", "/private/tmp/lc-sdk-cjson")),
    )
    parser.add_argument(
        "--plan", action="store_true",
        help="print the exact host-only commands without executing them",
    )
    parser.add_argument(
        "--timeout-seconds", type=_bounded_timeout, default=300,
        help="maximum runtime for each gate (1-3600 seconds; default: 300)",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _parser().parse_args(argv)
    repo = arguments.repo.resolve()
    try:
        plan = build_plan(
            repo, arguments.python, arguments.cjson_dir, arguments.esp_python)
    except ConfigurationError as error:
        print(f"ROADSHOW_SOFTWARE_GATES=CONFIG_ERROR {error}", file=sys.stderr)
        return 2

    if arguments.plan:
        for gate in plan:
            print(f"{gate.name}: {shlex.join(gate.command)}")
        print("ROADSHOW_SOFTWARE_GATES=NOT_RUN")
        return 0

    failed = False
    for gate in plan:
        print(f"RUN {gate.name}: {shlex.join(gate.command)}", flush=True)
        try:
            completed = subprocess.run(
                gate.command, cwd=repo, env=gate.environment, check=False,
                timeout=arguments.timeout_seconds)
        except subprocess.TimeoutExpired:
            print(f"GATE {gate.name} TIMEOUT", flush=True)
            failed = True
            continue
        except OSError:
            # Keep diagnostics static: command exceptions can contain private
            # local paths or environment-specific details.
            print(f"GATE {gate.name} ERROR", flush=True)
            failed = True
            continue
        passed = completed.returncode == 0
        print(f"GATE {gate.name} {'PASS' if passed else 'FAIL'}", flush=True)
        failed = failed or not passed
    print(f"ROADSHOW_SOFTWARE_GATES={'FAIL' if failed else 'PASS'}")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
