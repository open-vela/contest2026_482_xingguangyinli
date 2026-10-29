"""Read-only UTC clock gate. Does not set time, invoke ADB or contact NTP.

A PASS is only a current host/board clock comparison, not TLS, autonomous
NTP synchronization, cold-boot persistence or voice-service acceptance.
"""
import argparse
import json
import re
from datetime import datetime, timezone

MAX_SKEW_SECONDS = 600


def check_clock(raw, now=None):
    report = {"status": "FAIL", "max_skew_seconds": MAX_SKEW_SECONDS}
    if now is None:
        try:
            now = datetime.now(timezone.utc)
        except (OSError, OverflowError):
            report["reason"] = "unusable_reference_clock"
            return report
    if (not isinstance(now, datetime) or now.tzinfo is None or
            now.utcoffset() is None or not 2024 <= now.year <= 2099):
        report["reason"] = "unusable_reference_clock"
        return report
    if not isinstance(raw, str) or re.fullmatch(
            r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z", raw) is None:
        report["reason"] = "unparseable_board_utc"
        return report
    try:
        board = datetime.strptime(raw, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
    except ValueError:
        report["reason"] = "unparseable_board_utc"
        return report
    skew = (board - now.astimezone(timezone.utc)).total_seconds()
    report["skew_seconds"] = round(skew, 3)
    if abs(skew) <= MAX_SKEW_SECONDS:
        report.update(status="PASS", reason="within_window")
    else:
        report["reason"] = "clock_skew"
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board-date", required=True)
    args = parser.parse_args()
    report = check_clock(args.board_date)
    print(json.dumps(report, separators=(",", ":")))
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
