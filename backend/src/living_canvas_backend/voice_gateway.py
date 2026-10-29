"""Bounded Qwen speech adapter. No device capture, public endpoint or logging."""
import base64
import io
import json
import os
import time
import wave
from typing import Optional

import httpx


MAX_AUDIO_SECONDS = 15
MAX_CAPTURE_BYTES = 1_500_000
MAX_TTS_PCM_BYTES = 24_000 * 2 * MAX_AUDIO_SECONDS
MAX_TTS_TEXT = 72
PROVIDER_TIMEOUT = httpx.Timeout(10.0, connect=5.0, write=5.0, pool=5.0)


class VoiceGatewayError(RuntimeError):
    """Safe error code only; provider messages and credentials stay private."""


class QwenVoiceGateway:
    def __init__(self, api_key: str, client: Optional[httpx.Client] = None):
        self._api_key = api_key
        self.client = client or httpx.Client(
            timeout=PROVIDER_TIMEOUT,
            follow_redirects=False,
            trust_env=False,
        )

    @classmethod
    def from_env(cls):
        return cls(os.environ.get("LIVING_CANVAS_VOICE_API_KEY", ""))

    @property
    def enabled(self):
        return bool(self._api_key)

    def _require_enabled(self):
        if not self.enabled:
            raise VoiceGatewayError("voice_not_configured")

    def _headers(self):
        return {"Authorization": "Bearer " + self._api_key}

    @staticmethod
    def _provider_error_code(message):
        return ("voice_account_unavailable" if isinstance(message, dict)
                and message.get("code") == "Arrearage" else "voice_provider_failed")

    def _ensure_success(self, response, deadline):
        if response.is_success:
            return
        body = bytearray()
        for chunk in response.iter_bytes():
            if len(body) + len(chunk) > 8192 or time.monotonic() > deadline:
                raise VoiceGatewayError("voice_provider_failed")
            body.extend(chunk)
        message = {}
        try:
            candidates = [body] if body.lstrip().startswith(b"{") else [
                line[len(b"data:"):].strip() for line in body.splitlines()
                if line.startswith(b"data:")
            ]
            for value in candidates:
                parsed = json.loads(value)
                if isinstance(parsed, dict) and parsed.get("code") == "Arrearage":
                    message = parsed
                    break
        except (ValueError, RecursionError):
            pass
        raise VoiceGatewayError(self._provider_error_code(message))

    @staticmethod
    def _validate_capture(audio: bytes):
        try:
            if not isinstance(audio, bytes) or not audio or len(audio) > MAX_CAPTURE_BYTES:
                raise ValueError()
            with wave.open(io.BytesIO(audio)) as wav:
                frames, rate = wav.getnframes(), wav.getframerate()
                if (wav.getnchannels() != 1 or wav.getsampwidth() != 2
                        or rate not in (8000, 16000, 24000, 48000)
                        or wav.getcomptype() != "NONE" or not frames
                        or frames > MAX_AUDIO_SECONDS * rate
                        or len(wav.readframes(frames)) != frames * 2):
                    raise ValueError()
        except (ValueError, EOFError, wave.Error):
            raise VoiceGatewayError("voice_input_invalid") from None

    def transcribe(self, audio: bytes) -> str:
        self._require_enabled()
        self._validate_capture(audio)
        payload = {
            "model": "qwen3-asr-flash", "stream": False,
            "messages": [{"role": "user", "content": [{"type": "input_audio",
                "input_audio": {"data": "data:audio/wav;base64," + base64.b64encode(audio).decode()}}]}],
            "asr_options": {"enable_itn": False},
        }
        deadline = time.monotonic() + 25.0
        try:
            with self.client.stream("POST",
                    "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions",
                    headers=self._headers(), json=payload, timeout=PROVIDER_TIMEOUT) as response:
                self._ensure_success(response, deadline)
                body = bytearray()
                for chunk in response.iter_bytes():
                    if len(body) + len(chunk) > 65536 or time.monotonic() > deadline:
                        raise VoiceGatewayError("voice_provider_invalid")
                    body.extend(chunk)
                transcript = json.loads(body)["choices"][0]["message"]["content"]
        except (httpx.HTTPError, ValueError, KeyError, IndexError, TypeError, RecursionError):
            raise VoiceGatewayError("voice_provider_failed") from None
        if not isinstance(transcript, str) or not transcript.strip() or len(transcript.strip()) > 500:
            raise VoiceGatewayError("voice_transcript_invalid")
        return transcript.strip()

    def synthesize(self, text: str) -> bytes:
        self._require_enabled()
        if not isinstance(text, str) or not text.strip() or len(text.strip()) > MAX_TTS_TEXT:
            raise VoiceGatewayError("voice_input_invalid")
        headers = {**self._headers(), "X-DashScope-SSE": "enable"}
        payload = {"model": "qwen3-tts-flash", "input": {
            "text": text.strip(), "voice": "Cherry", "language_type": "Chinese",
        }}
        pcm = bytearray()
        buffered = bytearray()
        finished = False
        received = 0
        deadline = time.monotonic() + 25.0
        try:
            with self.client.stream("POST",
                    "https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation",
                    headers=headers, json=payload, timeout=PROVIDER_TIMEOUT) as response:
                self._ensure_success(response, deadline)
                for chunk in response.iter_bytes():
                    received += len(chunk)
                    if received > 1_500_000 or time.monotonic() > deadline:
                        raise VoiceGatewayError("voice_provider_limit")
                    buffered.extend(chunk)
                    while b"\n" in buffered:
                        line, _, remainder = buffered.partition(b"\n")
                        buffered = bytearray(remainder)
                        if not line.startswith(b"data:"):
                            continue
                        value = line[len(b"data:"):].strip()
                        if value == b"[DONE]":
                            continue
                        message = json.loads(value)
                        if message.get("code"):
                            raise VoiceGatewayError(self._provider_error_code(message))
                        output = message["output"]
                        encoded = output.get("audio", {}).get("data")
                        if encoded:
                            if finished:
                                raise VoiceGatewayError("voice_provider_invalid")
                            pcm.extend(base64.b64decode(encoded, validate=True))
                            if len(pcm) > MAX_TTS_PCM_BYTES:
                                raise VoiceGatewayError("voice_provider_limit")
                        finished = finished or output.get("finish_reason") == "stop"
                    if len(buffered) > 262144:
                        raise VoiceGatewayError("voice_provider_limit")
        except (httpx.HTTPError, ValueError, KeyError, TypeError, AttributeError, RecursionError):
            raise VoiceGatewayError("voice_provider_failed") from None
        if not finished or not pcm or len(pcm) % 2 or buffered.strip():
            raise VoiceGatewayError("voice_provider_incomplete")
        result = io.BytesIO()
        with wave.open(result, "wb") as wav:
            wav.setnchannels(1)
            wav.setsampwidth(2)
            wav.setframerate(24000)
            wav.writeframes(pcm)
        return result.getvalue()
