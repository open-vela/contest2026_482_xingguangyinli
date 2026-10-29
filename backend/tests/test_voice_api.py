import asyncio
import io
import threading
import wave
from concurrent.futures import ThreadPoolExecutor

import httpx
import pytest
from fastapi.testclient import TestClient
from fastapi import HTTPException
from starlette.requests import Request

from living_canvas_backend.app import create_app
from living_canvas_backend.voice_gateway import QwenVoiceGateway
from test_voice_gateway import wav_bytes, event


TOKEN = "lc-owner-synthetic-device-access-token-32-characters"
AUTH = {"Authorization": "Bearer " + TOKEN}


def test_disconnected_upload_is_bounded_error_without_provider_charge(tmp_path):
    client, sid, calls = configured(tmp_path)
    chunks = iter([
        {"type": "http.request", "body": wav_bytes()[:100], "more_body": True},
        {"type": "http.disconnect"},
    ])

    async def receive():
        return next(chunks)

    request = Request({"type": "http", "method": "POST", "path": "/v1/voice/asr",
        "headers": [(b"content-type", b"audio/wav"),
                    (b"authorization", ("Bearer " + TOKEN).encode())]}, receive)
    endpoint = next(route.endpoint for route in client.app.routes
                    if route.path == "/v1/voice/asr")
    with pytest.raises(HTTPException) as failure:
        asyncio.run(endpoint(request, sid))
    assert failure.value.status_code == 400
    assert failure.value.detail == "voice_upload_interrupted"
    assert calls == []


def configured(tmp_path, handler=None):
    calls = []

    def respond(request):
        calls.append(request.url.path)
        if handler:
            return handler(request)
        if request.url.path.endswith("/chat/completions"):
            return httpx.Response(200, json={"choices": [{"message": {"content": "我想吃面食"}}]})
        return httpx.Response(200, text=event(b"\x00\x00" * 100, "stop"))

    voice = QwenVoiceGateway("synthetic-provider-key", client=httpx.Client(transport=httpx.MockTransport(respond)))
    client = TestClient(create_app(data_dir=tmp_path, voice_gateway=voice, voice_device_token=TOKEN))
    sid = client.post(
        "/v1/session", json={"device_id": "frame-a"}, headers=AUTH
    ).json()["session_id"]
    return client, sid, calls


def test_voice_endpoints_are_disabled_without_explicit_configuration(tmp_path):
    client = TestClient(create_app(data_dir=tmp_path, voice_device_token=""))
    for path, body, mime in (("asr", wav_bytes(), "audio/wav"), ("tts", "你好".encode(), "text/plain")):
        response = client.post("/v1/voice/" + path + "?session_id=missing", content=body, headers={"Content-Type": mime})
        assert response.status_code == 503
        assert response.json()["detail"] == "voice_not_configured"


def test_voice_requires_device_authorization_before_provider_or_body_processing(tmp_path):
    client, sid, calls = configured(tmp_path)
    for header in ({}, {"Authorization": "Bearer wrong"}, {"Authorization": "Basic bad"}):
        response = client.post("/v1/voice/asr?session_id=" + sid, content=wav_bytes(), headers={**header, "Content-Type": "audio/wav"})
        assert response.status_code == 401
    assert calls == []


def test_unknown_session_does_not_charge_provider(tmp_path):
    client, _, calls = configured(tmp_path)
    response = client.post("/v1/voice/asr?session_id=missing", content=wav_bytes(), headers={**AUTH, "Content-Type": "audio/wav"})
    assert response.status_code == 404
    assert calls == []


def test_authenticated_asr_returns_text_without_automatically_confirming(tmp_path):
    client, sid, calls = configured(tmp_path)
    response = client.post("/v1/voice/asr?session_id=" + sid, content=wav_bytes(), headers={**AUTH, "Content-Type": "audio/wav"})
    assert response.status_code == 200
    assert response.json() == {"session_id": sid, "transcript": "我想吃面食"}
    assert client.app.state.sessions.get(sid).state.value == "created"
    assert len(calls) == 1


def test_voice_rejects_wrong_type_or_oversized_upload_before_provider(tmp_path):
    client, sid, calls = configured(tmp_path)
    assert client.post("/v1/voice/asr?session_id=" + sid, content=wav_bytes(), headers={**AUTH, "Content-Type": "text/plain"}).status_code == 415
    assert client.post("/v1/voice/asr?session_id=" + sid, content=b"x" * 1_500_001, headers={**AUTH, "Content-Type": "audio/wav"}).status_code == 413
    assert calls == []


def test_invalid_capture_is_not_a_success(tmp_path):
    client, sid, calls = configured(tmp_path)
    response = client.post("/v1/voice/asr?session_id=" + sid, content=b"not-wav", headers={**AUTH, "Content-Type": "audio/wav"})
    assert response.status_code == 400
    assert response.json()["detail"] == "voice_input_invalid"
    assert calls == []


def test_authenticated_tts_returns_playable_audio(tmp_path):
    client, sid, calls = configured(tmp_path)
    response = client.post("/v1/voice/tts?session_id=" + sid, content="你好".encode(), headers={**AUTH, "Content-Type": "text/plain; charset=utf-8"})
    assert response.status_code == 200
    assert response.headers["content-type"] == "audio/wav"
    assert response.headers["cache-control"] == "no-store"
    with wave.open(io.BytesIO(response.content)) as wav:
        assert wav.getframerate() == 24000
        assert wav.getnchannels() == 1
    assert len(calls) == 1


def test_invalid_tts_text_does_not_reach_provider(tmp_path):
    client, sid, calls = configured(tmp_path)
    for body in (b"", b"\xff", "字".encode() * 73):
        response = client.post("/v1/voice/tts?session_id=" + sid, content=body, headers={**AUTH, "Content-Type": "text/plain"})
        assert response.status_code in (400, 413)
    assert calls == []


def test_account_error_is_safe_and_does_not_create_recommendation(tmp_path):
    client, sid, _ = configured(tmp_path, lambda _: httpx.Response(400, json={"code": "Arrearage", "message": "PRIVATE_PROVIDER_DETAILS"}))
    response = client.post("/v1/voice/asr?session_id=" + sid, content=wav_bytes(), headers={**AUTH, "Content-Type": "audio/wav"})
    assert response.status_code == 503
    assert response.json()["detail"] == "voice_account_unavailable"
    assert "PRIVATE_PROVIDER_DETAILS" not in response.text
    assert client.app.state.sessions.get(sid).recommendation is None


def test_process_hourly_call_cap_limits_billed_requests(tmp_path):
    client, sid, calls = configured(tmp_path)
    for _ in range(60):
        assert client.post("/v1/voice/asr?session_id=" + sid, content=wav_bytes(), headers={**AUTH, "Content-Type": "audio/wav"}).status_code == 200
    response = client.post("/v1/voice/asr?session_id=" + sid, content=wav_bytes(), headers={**AUTH, "Content-Type": "audio/wav"})
    assert response.status_code == 429
    assert len(calls) == 60


def test_cancel_during_audio_upload_prevents_provider_call(tmp_path):
    async def check(path, payload, mime):
        client, sid, calls = configured(tmp_path / path)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=client.app),
                                     base_url="http://test") as remote:
            async def upload():
                yield payload[:1]
                cancelled = await remote.post("/v1/device/event", json={
                    "session_id": sid, "event": "cancel"}, headers=AUTH)
                assert cancelled.status_code == 200
                yield payload[1:]

            response = await remote.post("/v1/voice/" + path + "?session_id=" + sid,
                                         content=upload(), headers={**AUTH, "Content-Type": mime})
        assert response.status_code == 409
        assert response.json()["detail"] == "voice_session_cancelled"
        assert calls == []

    asyncio.run(check("asr", wav_bytes(), "audio/wav"))
    asyncio.run(check("tts", "你好".encode(), "text/plain"))


def test_cancelled_http_request_keeps_provider_concurrency_guard(tmp_path):
    entered = threading.Event()
    release = threading.Event()
    finished = threading.Event()

    def delayed(request):
        if entered.is_set():
            return httpx.Response(200, json={"choices": [{"message": {"content": "面食"}}]})
        entered.set()
        try:
            assert release.wait(5)
            return httpx.Response(200, json={"choices": [{"message": {"content": "面食"}}]})
        finally:
            finished.set()

    async def check():
        client, sid, calls = configured(tmp_path, delayed)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=client.app),
                                     base_url="http://test") as remote:
            async def send():
                return await remote.post("/v1/voice/asr?session_id=" + sid,
                                         content=wav_bytes(), headers={**AUTH, "Content-Type": "audio/wav"})
            first = asyncio.create_task(send())
            try:
                assert await asyncio.to_thread(entered.wait, 3)
                first.cancel()
                try:
                    await first
                except asyncio.CancelledError:
                    pass
                second = await send()
                assert second.status_code == 429
                assert len(calls) == 1
            finally:
                release.set()
                assert await asyncio.to_thread(finished.wait, 3)
            # Let the completion notification reach the event loop.
            await asyncio.sleep(0)
            assert (await send()).status_code == 200

    asyncio.run(check())


def test_queued_cancelled_request_releases_guard_without_provider_cost(tmp_path, monkeypatch):
    release = threading.Event()

    async def check():
        client, sid, calls = configured(tmp_path)
        loop = asyncio.get_running_loop()
        loop.set_default_executor(ThreadPoolExecutor(max_workers=1))
        occupied = loop.run_in_executor(None, release.wait, 5)
        queued = []
        original_submit = loop.run_in_executor

        def submit(*args):
            future = original_submit(*args)
            queued.append(future)
            return future

        monkeypatch.setattr(loop, "run_in_executor", submit)
        async with httpx.AsyncClient(transport=httpx.ASGITransport(app=client.app),
                                     base_url="http://test") as remote:
            async def send(session):
                return await remote.post("/v1/voice/asr?session_id=" + session,
                                         content=wav_bytes(), headers={**AUTH, "Content-Type": "audio/wav"})
            first = asyncio.create_task(send(sid))
            try:
                for _ in range(20):
                    if queued:
                        break
                    await asyncio.sleep(0)
                assert len(queued) == 1
                first.cancel()
                try:
                    await first
                except asyncio.CancelledError:
                    pass
                assert (await remote.post("/v1/device/event", json={
                    "session_id": sid, "event": "cancel"}, headers=AUTH)).status_code == 200
                new_sid = (await remote.post(
                    "/v1/session", json={"device_id": "frame-b"}, headers=AUTH
                )).json()["session_id"]
                assert (await send(new_sid)).status_code == 429
                assert calls == []
                release.set()
                await occupied
                try:
                    await queued[0]
                except HTTPException as error:
                    assert error.status_code == 409
                else:
                    raise AssertionError("Cancelled session reached the provider")
                assert calls == []
                assert (await send(new_sid)).status_code == 200
                assert len(calls) == 1
            finally:
                release.set()
                await occupied

    asyncio.run(check())
