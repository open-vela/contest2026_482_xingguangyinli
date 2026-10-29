#!/usr/bin/env python3
"""Verify the Living Canvas P0 flow against its independent backend.

This intentionally uses only the Python standard library so it can run from
the Mac or the openvela build VM without installing another client package.
"""

from __future__ import annotations

import argparse
import ipaddress
import json
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request
from typing import Optional


RFC1918 = tuple(ipaddress.ip_network(value) for value in (
    "10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"
))


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def direct_opener():
    """Build a client that cannot inherit proxy settings or follow redirects."""
    return urllib.request.build_opener(
        urllib.request.ProxyHandler({}),
        NoRedirect(),
    )


def validate_base_url(value: str) -> str:
    try:
        parsed = urllib.parse.urlsplit(value)
        port = parsed.port
        if parsed.scheme == "http":
            address = ipaddress.ip_address(parsed.hostname or "")
            safe_transport = address.is_loopback or any(
                address in network for network in RFC1918
            )
        else:
            safe_transport = parsed.scheme == "https"
        if (
            not safe_transport
            or not parsed.hostname
            or parsed.username is not None
            or parsed.password is not None
            or parsed.query
            or parsed.fragment
            or parsed.path not in ("", "/")
            or (port is not None and not 1 <= port <= 65535)
            or any(ord(character) <= 0x20 or ord(character) >= 0x7f
                   for character in value)
        ):
            raise ValueError()
    except ValueError:
        raise RuntimeError("invalid backend URL") from None
    return value.rstrip("/")


def request_json(
    base_url: str,
    path: str,
    body: Optional[dict] = None,
    device_token: str = "",
    opener=None,
) -> dict:
    base_url = validate_base_url(base_url)
    data = None
    headers = {"Accept": "application/json"}
    method = "GET"
    if body is not None:
        require(re.fullmatch(r"lc-owner-[-A-Za-z0-9._]{23,119}", device_token)
                is not None, "owner-device token is not configured")
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        headers["Content-Type"] = "application/json"
        headers["Authorization"] = "Bearer " + device_token
        method = "POST"

    request = urllib.request.Request(
        urllib.parse.urljoin(base_url + "/", path.lstrip("/")),
        data=data,
        headers=headers,
        method=method,
    )
    opener = opener or direct_opener()
    try:
        with opener.open(request, timeout=15) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        error.close()
        raise RuntimeError(f"{path} returned HTTP {error.code}") from error
    except urllib.error.URLError as error:
        raise RuntimeError("cannot reach Living Canvas backend") from error


def verify_redirect(base_url: str, token: str, expected_url: str,
                    opener=None) -> None:
    base_url = validate_base_url(base_url)
    opener = opener or direct_opener()
    request = urllib.request.Request(
        urllib.parse.urljoin(base_url + "/", f"c/{token}"), method="GET"
    )
    try:
        opener.open(request, timeout=15)
    except urllib.error.HTTPError as error:
        if error.code != 307:
            raise RuntimeError(f"compact handoff returned HTTP {error.code}") from error
        actual = error.headers.get("Location", "")
        if actual != expected_url:
            raise RuntimeError(
                f"compact handoff location mismatch: expected {expected_url}, got {actual}"
            )
        parsed = urllib.parse.urlparse(actual)
        if parsed.scheme != "https" or parsed.hostname != "h5.ele.me":
            raise RuntimeError("compact handoff target is not the allowed Ele.me host")
        return
    raise RuntimeError("compact handoff did not return HTTP 307")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def validate_source(source: object, fallback: object, require_model: bool) -> None:
    require(source in {"model", "rules"},
            f"decision has an invalid source marker: {source}")
    require(isinstance(fallback, bool),
            "decision has no boolean rules_fallback marker")
    require((source == "rules") is fallback,
            "source and rules_fallback markers disagree")
    require(not require_model or source == "model",
            "decision is not model-backed (source=model required)")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--base-url",
        default="http://127.0.0.1:8765",
        help="running Living Canvas backend URL",
    )
    parser.add_argument(
        "--require-model",
        action="store_true",
        help="fail if the recommendation used the deterministic rules fallback",
    )
    args = parser.parse_args()
    base_url = validate_base_url(args.base_url)
    device_token = os.environ.get("LIVING_CANVAS_VOICE_DEVICE_TOKEN", "")
    require(re.fullmatch(r"lc-owner-[-A-Za-z0-9._]{23,119}", device_token)
            is not None,
            "LIVING_CANVAS_VOICE_DEVICE_TOKEN is not configured")

    health = request_json(base_url, "/health")
    require(
        health.get("service") == "living-canvas-decision-backend"
        and health.get("status") == "ok",
        "Living Canvas health check is not ok",
    )

    created = request_json(
        base_url,
        "/v1/session",
        {"device_id": "gemini-s1"},
        device_token=device_token,
    )
    session_id = str(created.get("session_id", ""))
    require(session_id.startswith("meal_") and len(session_id) == 17,
            "backend returned an invalid session id")

    decision = request_json(
        base_url,
        "/v1/dialogue",
        {
            "session_id": session_id,
            "choice": "takeout",
            "transcript": "想吃热乎的米饭，预算四十元",
        },
        device_token=device_token,
    )
    require(decision.get("state") == "candidate",
            f"decision did not reach candidate state: {decision.get('state')}")
    require(decision.get("choice") == "takeout",
            f"dialogue choice mismatch: {decision.get('choice')}")
    recommendation = decision.get("recommendation") or {}
    require(bool(recommendation.get("candidate")), "decision has no candidate")
    source = recommendation.get("source")
    fallback = recommendation.get("rules_fallback")
    validate_source(source, fallback, require_model=args.require_model)

    roles = decision.get("role_contributions") or []
    role_ids = {item.get("role_id") for item in roles if isinstance(item, dict)}
    require(role_ids == {"beagle", "cat", "pigeon", "pig", "alien"},
            f"dialogue role set mismatch: {sorted(role_ids)}")
    require(
        all(isinstance(item.get("summary"), str) and item["summary"].strip()
            for item in roles),
        "dialogue contains an empty role contribution",
    )
    require(bool(str(decision.get("spoken_reply", "")).strip()),
            "dialogue has no spoken reply")
    require(decision.get("requires_handoff") is True,
            "takeout dialogue must require a phone handoff")

    event = request_json(
        base_url,
        "/v1/device/event",
        {
            "session_id": session_id,
            "event": "accept_candidate",
        },
        device_token=device_token,
    )
    require(event.get("state") == "confirming",
            f"device confirmation failed: {event.get('state')}")

    confirmed = request_json(
        base_url,
        "/v1/confirm",
        {"session_id": session_id, "platform": "eleme"},
        device_token=device_token,
    )
    require(confirmed.get("state") == "handed_off", "backend confirmation failed")
    require(
        str(confirmed.get("external_url", "")).startswith("https://"),
        "backend did not return a phone-safe web URL",
    )
    require(
        confirmed.get("requires_user_payment") is True,
        "handoff must keep final payment with the user",
    )

    token = session_id.removeprefix("meal_")
    verify_redirect(base_url, token, str(confirmed["external_url"]))

    chosen_id = recommendation["candidate"].get("candidate_id", "")
    print(
        f"PASS: Living Canvas takeout flow session={session_id} "
        f"candidate={chosen_id} source={source} roles={len(roles)}"
    )
    print(
        f"PASS: compact handoff {base_url}/c/{token} "
        "-> https://h5.ele.me/..."
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)
