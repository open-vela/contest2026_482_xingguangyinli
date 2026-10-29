#!/usr/bin/env python3
"""Bounded voice deployment/config/auth probe; no recording or provider request.

Only /health and empty ASR/TTS requests with an impossible meal-session ID are
sent. PASS does not prove provider balance, microphone, speaker, or speech flow.
Device credentials are accepted through stdin only, never command arguments.
"""
import argparse
import http.client
import io
import json
import re
import sys
import urllib.error
import urllib.parse
import urllib.request


class PreflightError(Exception):
    """Static safe diagnostics, never response bodies or credential values."""


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def direct_opener():
    """Build a client that cannot inherit proxy settings or follow redirects."""
    return urllib.request.build_opener(
        urllib.request.ProxyHandler({}),
        NoRedirect(),
    )


def validate_base_url(value):
    try:
        parsed = urllib.parse.urlsplit(value)
        port = parsed.port
        safe_transport = parsed.scheme == "https" or (
            parsed.scheme == "http" and parsed.hostname in ("127.0.0.1", "::1"))
        if (not safe_transport or not parsed.hostname or parsed.username is not None
            or parsed.password is not None or parsed.query or parsed.fragment
            or any(ord(c) <= 32 or ord(c) >= 127 for c in value)
            or any(ord(c) <= 32 for c in urllib.parse.unquote(parsed.path))
            or (port is not None and port < 1)):
            raise ValueError()
    except ValueError:
        raise PreflightError("invalid_base_url") from None
    return value.rstrip("/")


def read_token(stream):
    token = stream.read(131)
    if token.endswith("\r\n"):
        token = token[:-2]
    elif token.endswith("\n"):
        token = token[:-1]
    if (not token.startswith("lc-owner-") or
        not re.fullmatch(r"[-A-Za-z0-9._]{32,128}", token)):
        raise PreflightError("invalid_device_token")
    return token


def probe(opener, base, endpoint, token=None):
    voice = endpoint != "/health"
    headers = {"Accept": "application/json"}
    if voice:
        headers["Content-Type"] = "audio/wav" if "/asr?" in endpoint else "text/plain"
    if token is not None:
        headers["Authorization"] = "Bearer " + token
    request = urllib.request.Request(base + endpoint, data=b"" if voice else None,
        headers=headers, method="POST" if voice else "GET")
    try:
        try:
            response = opener.open(request, timeout=5)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            status = response.code
            raw = response.read(4097)
        if len(raw) > 4096:
            raise PreflightError("response_too_large")
        data = json.loads(raw)
        if not isinstance(data, dict):
            raise ValueError()
    except (urllib.error.URLError, OSError, http.client.HTTPException):
        raise PreflightError("transport_unavailable") from None
    except (ValueError, UnicodeError):
        raise PreflightError("response_invalid") from None
    return status, data


def verify(base_url, token=None, opener=None):
    base = validate_base_url(base_url)
    if token is not None:
        token = read_token(io.StringIO(token))
    opener = opener or direct_opener()
    status, data = probe(opener, base, "/health")
    if status != 200 or data.get("status") != "ok" or data.get("service") != "living-canvas-decision-backend":
        raise PreflightError("health_unavailable")
    count = 1
    for operation in ("asr", "tts"):
        # SessionStore generates only meal_<12hex>; this can never be a real ID.
        endpoint = "/v1/voice/" + operation + "?session_id=voice-preflight-no-session"
        status, data = probe(opener, base, endpoint)
        count += 1
        if status == 503 and data.get("detail") == "voice_not_configured":
            raise PreflightError(operation + "_not_configured")
        if status != 401 or data.get("detail") != "voice_device_unauthorized":
            raise PreflightError(operation + "_route_or_auth_unavailable")
        if token is not None:
            status, data = probe(opener, base, endpoint, token)
            count += 1
            if status == 401:
                raise PreflightError("device_token_rejected")
            if status != 404 or data.get("detail") != "session_not_found":
                raise PreflightError(operation + "_authenticated_gate_unavailable")
    return {"status": "PASS", "device_authorization": "ACCEPTED" if token else "NOT_TESTED",
        "provider_access": "NOT_TESTED", "device_capture": "NOT_TESTED", "requests": count}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-url", required=True,
        help="explicit HTTPS backend URL, or loopback HTTP for local tests")
    parser.add_argument("--token-stdin", action="store_true",
        help="read separate owner-device token from stdin; never a Qwen provider key")
    args = parser.parse_args()
    try:
        token = read_token(sys.stdin) if args.token_stdin else None
        result = verify(args.base_url, token)
    except PreflightError as error:
        print(json.dumps({"status": "FAIL", "reason": str(error)}))
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
