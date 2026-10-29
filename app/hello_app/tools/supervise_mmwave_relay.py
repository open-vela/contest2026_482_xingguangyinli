#!/usr/bin/env python3
"""Supervise one bounded, privacy-minimal MR60-to-backend sample."""

from __future__ import annotations

import argparse
import io
import json
import os
import secrets
import sys

from accessory_http_relay import AccessoryHttpRelay
from resolve_mmwave_port import configured_mmwave_serial
from sample_mmwave_presence import OPERATOR_CONFIRMATION, sample_presence


def confirmed_ports(operator_confirmation, port_provider):
    """Do not enumerate any host device before the fresh physical gate."""
    if operator_confirmation != OPERATOR_CONFIRMATION:
        raise ValueError("fresh operator confirmation required")
    return port_provider()


def run_relay_sample(
    ports,
    operator_confirmation,
    relay,
    output_stream,
    error_stream,
    *,
    expected_serial,
    **sample_dependencies,
):
    boolean_output = io.StringIO()
    sample_error = io.StringIO()
    status = sample_presence(
        ports,
        operator_confirmation,
        boolean_output,
        sample_error,
        expected_serial=expected_serial,
        **sample_dependencies,
    )
    if status != 0:
        print("verified mmWave sample unavailable", file=error_stream)
        return status

    try:
        lines = boolean_output.getvalue().splitlines()
        if not lines:
            raise ValueError("no observations")
        for line in lines:
            observation = json.loads(line)
            if (
                not isinstance(observation, dict)
                or set(observation) != {"detected"}
                or type(observation["detected"]) is not bool
            ):
                raise ValueError("invalid observation")
            if not relay.deliver(observation):
                print("verified mmWave delivery unavailable", file=error_stream)
                return 1
    except (KeyError, TypeError, ValueError, json.JSONDecodeError):
        print("verified mmWave observation unavailable", file=error_stream)
        return 2

    print("MMWAVE_RELAY_SAMPLE_OK", file=output_stream)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--execute", action="store_true")
    parser.add_argument("--operator-confirmation", default="")
    args = parser.parse_args()
    if not args.execute:
        print("dry run: bounded verified mmWave relay sample is ready")
        return 0

    try:
        from serial.tools import list_ports

        endpoint = os.environ.get("LC_ACCESSORY_URL", "")
        token = os.environ.get("LC_ACCESSORY_DEVICE_TOKEN", "")
        session_id = os.environ.get(
            "LC_ACCESSORY_SESSION_ID", "mw_" + secrets.token_hex(8)
        )
        relay = AccessoryHttpRelay(endpoint, token, session_id)
        return run_relay_sample(
            confirmed_ports(args.operator_confirmation, list_ports.comports),
            args.operator_confirmation,
            relay,
            sys.stdout,
            sys.stderr,
            expected_serial=configured_mmwave_serial(),
        )
    except (ImportError, OSError, ValueError):
        print("verified mmWave relay sample unavailable", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
