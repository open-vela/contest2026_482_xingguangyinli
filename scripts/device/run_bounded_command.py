#!/usr/bin/env python3
"""Run one command with a hard deadline and process-group cleanup."""

import argparse
import math
import os
import signal
import subprocess
import sys


USAGE_ERROR = 64
TIMEOUT_EXIT = 124


def terminate_process_group(proc):
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        proc.wait(timeout=0.5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def run_bounded(command, timeout):
    proc = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )
    timed_out = False
    try:
        stdout, stderr = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        terminate_process_group(proc)
        stdout, stderr = proc.communicate()

    if stdout:
        sys.stdout.buffer.write(stdout)
    if stderr:
        sys.stderr.buffer.write(stderr)
    if timed_out:
        print(f"ERROR: command timed out after {timeout:g} seconds", file=sys.stderr)
        return TIMEOUT_EXIT
    return proc.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", required=True, type=float)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if (not math.isfinite(args.timeout) or args.timeout <= 0
            or not command):
        print("ERROR: timeout must be positive and a command is required",
              file=sys.stderr)
        return USAGE_ERROR
    return run_bounded(command, args.timeout)


if __name__ == "__main__":
    raise SystemExit(main())
