"""Exercise the deployment probe against real backend HTTP, never billed voice."""
import importlib.util
import io
import json
import os
import socket
import subprocess
import sys
import threading
import time
import urllib.request
from pathlib import Path

import httpx
import pytest
import uvicorn
from starlette.responses import Response, RedirectResponse

from living_canvas_backend.app import create_app
from living_canvas_backend.voice_gateway import QwenVoiceGateway
from living_canvas_backend.model_gateway import ModelGateway

SCRIPT = Path(__file__).resolve().parents[1] / "verify_voice_preflight.py"
TOKEN = "lc-owner-synthetic-device-token-32-characters"


def load():
    assert SCRIPT.is_file(), "no-charge voice deployment probe is missing"
    spec = importlib.util.spec_from_file_location("voice_preflight", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_default_opener_disables_environment_proxies_and_redirects():
    module = load()
    sentinel = object()
    with pytest.MonkeyPatch.context() as patcher:
        build_calls = []

        def capture(*handlers):
            build_calls.append(handlers)
            return sentinel

        patcher.setattr(module.urllib.request, "build_opener", capture)
        assert module.direct_opener() is sentinel
    handlers = build_calls[0]
    proxy = next(
        handler for handler in handlers
        if isinstance(handler, module.urllib.request.ProxyHandler)
    )
    assert proxy.proxies == {}
    assert any(isinstance(handler, module.NoRedirect) for handler in handlers)


def loopback_listener():
    listener = socket.socket()
    try:
        listener.bind(("127.0.0.1", 0))
    except PermissionError:
        listener.close()
        pytest.skip("local loopback bind is blocked by the test sandbox")
    return listener


@pytest.mark.parametrize("url", ["http://api.example.invalid", "https://user:password@example.invalid",
    "https://example.invalid/?key=private", "https://example.invalid/#private",
    "https://example.invalid\n/path", "file:///tmp/private", "https://", "https://example.invalid:bad"])
def test_rejects_unsafe_urls_before_any_request(url):
    module = load()
    with pytest.raises(module.PreflightError, match="invalid_base_url"):
        module.validate_base_url(url)


@pytest.mark.parametrize("url", ["https://api.example.invalid", "http://127.0.0.1:8765",
    "http://[::1]:8765", "https://example.invalid/prefix"])
def test_accepts_https_or_numeric_loopback_only(url):
    assert load().validate_base_url(url + "/") == url


@pytest.mark.parametrize("token", ["", "short", "sk-" + "x" * 48,
    "x" * 32, "x" * 129, "x" * 32 + "\nsecond-line", "x" * 31 + " ",
    "x" * 31 + "\x00"])
def test_rejects_provider_keys_or_malformed_device_token_without_echo(token):
    module = load()
    with pytest.raises(module.PreflightError) as failure:
        module.read_token(io.StringIO(token))
    assert str(failure.value) == "invalid_device_token"


@pytest.mark.parametrize("suffix", ["", "\n", "\r\n"])
def test_token_stdin_accepts_one_complete_line(suffix):
    assert load().read_token(io.StringIO(TOKEN + suffix)) == TOKEN


@pytest.fixture
def backend(tmp_path, request):
    provider_calls = []

    def provider(request):
        provider_calls.append(request.url.path)
        raise AssertionError("preflight must never invoke a provider")

    mode = getattr(request, "param", "normal")
    provider_client = httpx.Client(transport=httpx.MockTransport(provider))
    model = ModelGateway("", "", "")
    gateway = QwenVoiceGateway("" if mode == "disabled" else "synthetic-provider-key",
        client=provider_client)
    app = create_app(data_dir=tmp_path / "data", voice_gateway=gateway, model_gateway=model,
        voice_device_token=TOKEN)

    if mode in ("redirect", "huge", "malformed", "missing"):
        @app.middleware("http")
        async def broken_deployment(request, next_handler):
            if request.url.path == "/unexpected-followed":
                provider_calls.append("REDIRECT_FOLLOWED")
                return Response("unsafe", status_code=200)
            if request.url.path.startswith("/v1/voice/"):
                if mode == "redirect":
                    return RedirectResponse("/unexpected-followed", status_code=307)
                if mode == "huge":
                    return Response("PRIVATE_UPSTREAM_DETAIL" * 1000, status_code=503)
                if mode == "malformed":
                    return Response("PRIVATE_UPSTREAM_DETAIL", status_code=502)
                return Response('{"detail":"Not Found"}', status_code=404,
                    media_type="application/json")
            return await next_handler(request)
    listener = loopback_listener()
    server = uvicorn.Server(uvicorn.Config(app, log_config=None, access_log=False, ws="none"))
    thread = threading.Thread(target=server.run, kwargs={"sockets": [listener]})
    thread.start()
    try:
        deadline = time.monotonic() + 5
        while not server.started and thread.is_alive() and time.monotonic() < deadline:
            time.sleep(0.01)
        assert server.started, "isolated actual backend did not start"
        yield "http://127.0.0.1:" + str(listener.getsockname()[1]), app, provider_calls
    finally:
        server.should_exit = True
        thread.join(5)
        listener.close()
        assert not thread.is_alive()
        provider_client.close()
        model.client.close()


@pytest.mark.parametrize("authenticated", [False, True])
def test_actual_http_preflight_never_creates_session_or_calls_provider(backend, authenticated):
    module = load()
    url, app, provider_calls = backend
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), module.NoRedirect())
    result = module.verify(url, TOKEN if authenticated else None, opener=opener)
    assert result["status"] == "PASS"
    assert result["device_authorization"] == ("ACCEPTED" if authenticated else "NOT_TESTED")
    assert result["provider_access"] == "NOT_TESTED"
    assert result["device_capture"] == "NOT_TESTED"
    assert result["requests"] == (5 if authenticated else 3)
    assert provider_calls == []
    assert app.state.sessions.get("voice-preflight-no-session") is None
    assert len(app.state.sessions._sessions) == 0


def test_actual_http_rejected_token_is_safe_failure(backend):
    module = load()
    url, _, calls = backend
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), module.NoRedirect())
    with pytest.raises(module.PreflightError, match="device_token_rejected") as failure:
        module.verify(url, "lc-owner-wrong-synthetic-token-32-characters", opener=opener)
    assert "synthetic" not in str(failure.value)
    assert calls == []


@pytest.mark.parametrize("backend,reason", [("disabled", "asr_not_configured"),
    ("missing", "asr_route_or_auth_unavailable"), ("huge", "response_too_large"),
    ("malformed", "response_invalid")], indirect=["backend"])
def test_actual_http_failure_modes_do_not_leak_response_or_call_provider(backend, reason):
    module = load()
    url, _, calls = backend
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), module.NoRedirect())
    with pytest.raises(module.PreflightError) as failure:
        module.verify(url, TOKEN, opener=opener)
    assert str(failure.value) == reason
    assert calls == []


@pytest.mark.parametrize("backend", ["redirect"], indirect=True)
def test_actual_http_redirect_cannot_forward_device_token(backend):
    module = load()
    url, _, calls = backend
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), module.NoRedirect())
    with pytest.raises(module.PreflightError, match="response_invalid"):
        module.probe(opener, url,
            "/v1/voice/asr?session_id=voice-preflight-no-session", TOKEN)
    assert calls == []


def test_actual_malformed_http_is_sanitized_not_raw_upstream_details():
    module = load()
    listener = loopback_listener()
    listener.listen(1)
    listener.settimeout(5)
    url = "http://127.0.0.1:" + str(listener.getsockname()[1])

    def invalid_http():
        with listener.accept()[0] as connection:
            connection.settimeout(5)
            connection.recv(4096)
            connection.sendall(b"PRIVATE_UPSTREAM_DETAIL\r\n\r\n")

    thread = threading.Thread(target=invalid_http)
    thread.start()
    try:
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), module.NoRedirect())
        with pytest.raises(module.PreflightError) as failure:
            module.verify(url, opener=opener)
        assert str(failure.value) == "transport_unavailable"
    finally:
        thread.join(5)
        listener.close()
        assert not thread.is_alive()


def test_actual_cli_reads_token_only_from_stdin_and_prints_no_token(backend):
    url, _, calls = backend
    result = subprocess.run([sys.executable, str(SCRIPT), "--base-url", url, "--token-stdin"],
        input=TOKEN + "\n", capture_output=True, text=True, timeout=10,
        env={"PATH": os.defpath, "NO_PROXY": "127.0.0.1"})
    assert result.returncode == 0, result.stdout + result.stderr
    assert TOKEN not in result.stdout + result.stderr
    assert json.loads(result.stdout)["device_authorization"] == "ACCEPTED"
    assert result.stderr == ""
    assert calls == []
