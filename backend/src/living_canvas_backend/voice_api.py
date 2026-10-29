"""Opt-in, authenticated audio endpoints; no automatic meal confirmation."""
import asyncio
import hmac
import threading
import time
from collections import deque

from fastapi import HTTPException, Query, Request, Response
from starlette.requests import ClientDisconnect

from .voice_gateway import MAX_CAPTURE_BYTES, MAX_TTS_TEXT, VoiceGatewayError


def register_voice_routes(app, sessions, gateway, device_token):
    # Prototype owner-device token, not a public multi-user authorization system.
    # Rate state is process-local: keep a single worker until a shared limiter exists.
    calls = deque()
    lock = threading.Lock()
    active = [False]

    def authorize(request):
        if not gateway.enabled or len(device_token) < 32:
            raise HTTPException(503, detail="voice_not_configured")
        supplied = request.headers.get("authorization", "").encode("utf-8")
        expected = ("Bearer " + device_token).encode("utf-8")
        if not hmac.compare_digest(supplied, expected):
            raise HTTPException(401, detail="voice_device_unauthorized")

    def require_session(session_id):
        session = sessions.get(session_id)
        if session is None:
            raise HTTPException(404, detail="session_not_found")
        if session.state.value == "cancelled":
            raise HTTPException(409, detail="voice_session_cancelled")
        return session

    async def body(request, mime, maximum):
        if request.headers.get("content-type", "").split(";", 1)[0].strip() != mime:
            raise HTTPException(415, detail="voice_content_type_invalid")
        declared = request.headers.get("content-length")
        if declared is not None:
            try:
                length = int(declared)
            except ValueError:
                raise HTTPException(400, detail="voice_content_length_invalid") from None
            if length < 0:
                raise HTTPException(400, detail="voice_content_length_invalid")
            if length > maximum:
                raise HTTPException(413, detail="voice_upload_too_large")
        result = bytearray()
        try:
            async for chunk in request.stream():
                if len(result) + len(chunk) > maximum:
                    raise HTTPException(413, detail="voice_upload_too_large")
                result.extend(chunk)
        except ClientDisconnect:
            raise HTTPException(400, detail="voice_upload_interrupted") from None
        return bytes(result)

    async def invoke(operation, value, session_id):
        with lock:
            now = time.monotonic()
            while calls and now - calls[0] >= 3600:
                calls.popleft()
            if active[0] or len(calls) >= 60:
                raise HTTPException(429, detail="voice_rate_limited")
            active[0] = True
            calls.append(now)
        def work():
            try:
                # Upload and executor queueing may outlive a session cancellation.
                require_session(session_id)
                return operation(value)
            except VoiceGatewayError as error:
                code = str(error)
                status = {"voice_not_configured": 503, "voice_account_unavailable": 503,
                          "voice_input_invalid": 400, "voice_transcript_invalid": 422}.get(code, 502)
                raise HTTPException(status, detail=code) from None
            finally:
                # An HTTP task cancellation does not stop a synchronous provider call.
                with lock:
                    active[0] = False

        try:
            future = asyncio.get_running_loop().run_in_executor(None, work)
        except BaseException:
            with lock:
                active[0] = False
            raise
        # Observe late failures even if the client has gone away; never log payloads.
        future.add_done_callback(lambda completed: completed.exception()
                                 if not completed.cancelled() else None)
        return await asyncio.shield(future)

    @app.post("/v1/voice/asr")
    async def transcribe(request: Request, session_id: str = Query(..., max_length=80)):
        authorize(request)
        require_session(session_id)
        capture = await body(request, "audio/wav", MAX_CAPTURE_BYTES)
        require_session(session_id)
        transcript = await invoke(gateway.transcribe, capture, session_id)
        require_session(session_id)
        return {"session_id": session_id, "transcript": transcript}

    @app.post("/v1/voice/tts")
    async def synthesize(request: Request, session_id: str = Query(..., max_length=80)):
        authorize(request)
        require_session(session_id)
        encoded = await body(request, "text/plain", MAX_TTS_TEXT * 4)
        try:
            text = encoded.decode("utf-8")
        except UnicodeError:
            raise HTTPException(400, detail="voice_input_invalid") from None
        require_session(session_id)
        audio = await invoke(gateway.synthesize, text, session_id)
        require_session(session_id)
        return Response(audio, media_type="audio/wav", headers={"Cache-Control": "no-store"})
