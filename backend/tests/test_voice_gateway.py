import base64
import io
import json
import wave
from unittest.mock import patch

import httpx
import pytest

from living_canvas_backend.voice_gateway import QwenVoiceGateway, VoiceGatewayError


def wav_bytes(frames=1600, rate=16000, channels=1, width=2):
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(channels)
        wav.setsampwidth(width)
        wav.setframerate(rate)
        wav.writeframes(b"\x00" * frames * channels * width)
    return output.getvalue()


def gateway(handler, key="synthetic-key"):
    return QwenVoiceGateway(key, client=httpx.Client(transport=httpx.MockTransport(handler)))


def fail_network(_request):
    pytest.fail("Invalid or disabled input must not reach provider")


def event(pcm=b"", finish=None):
    return "data: " + json.dumps({"output": {
        "audio": {"data": base64.b64encode(pcm).decode()}, "finish_reason": finish,
    }}) + "\n\n"


def test_missing_key_is_explicitly_unavailable():
    voice = gateway(fail_network, key="")
    assert not voice.enabled
    with pytest.raises(VoiceGatewayError, match="voice_not_configured"):
        voice.transcribe(wav_bytes())
    with pytest.raises(VoiceGatewayError, match="voice_not_configured"):
        voice.synthesize("你好")


def test_default_client_ignores_ambient_proxy_and_redirect_configuration():
    with patch("living_canvas_backend.voice_gateway.httpx.Client") as client:
        QwenVoiceGateway("synthetic-key")
    _, kwargs = client.call_args
    assert kwargs["trust_env"] is False
    assert kwargs["follow_redirects"] is False


@pytest.mark.parametrize("audio", [b"", b"not-wav", wav_bytes(frames=0),
    wav_bytes(channels=2), wav_bytes(width=1), wav_bytes(rate=11025),
    wav_bytes(frames=16 * 16000), wav_bytes()[:-2]])
def test_invalid_capture_is_rejected_before_network(audio):
    with pytest.raises(VoiceGatewayError, match="voice_input_invalid"):
        gateway(fail_network).transcribe(audio)


def test_asr_uses_valid_wav_and_returns_bounded_transcript():
    capture = wav_bytes()

    def respond(request):
        assert str(request.url) == "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"
        assert request.headers["authorization"] == "Bearer synthetic-key"
        payload = json.loads(request.content)
        assert payload["model"] == "qwen3-asr-flash"
        encoded = payload["messages"][0]["content"][0]["input_audio"]["data"]
        assert base64.b64decode(encoded.split(",", 1)[1]) == capture
        return httpx.Response(200, json={"choices": [{"message": {"content": "  我想吃面食  "}}]})

    assert gateway(respond).transcribe(capture) == "我想吃面食"


@pytest.mark.parametrize("content", ["", "  ", None, ["bad"], "面" * 501])
def test_asr_empty_or_invalid_transcript_is_not_success(content):
    voice = gateway(lambda _: httpx.Response(200, json={"choices": [{"message": {"content": content}}]}))
    with pytest.raises(VoiceGatewayError, match="voice_transcript_invalid"):
        voice.transcribe(wav_bytes())


@pytest.mark.parametrize("status", [401, 403, 429, 500])
def test_provider_http_error_does_not_expose_key_or_body(status):
    voice = gateway(lambda _: httpx.Response(status, text="synthetic-key PRIVATE_TRANSCRIPT"))
    for call in (lambda: voice.transcribe(wav_bytes()), lambda: voice.synthesize("你好")):
        with pytest.raises(VoiceGatewayError) as error:
            call()
        assert str(error.value) == "voice_provider_failed"
        assert "synthetic-key" not in str(error.value)


def test_timeout_returns_explicit_failure_without_retry():
    calls = []

    def timeout(request):
        calls.append(request)
        raise httpx.ReadTimeout("private provider diagnostics", request=request)

    with pytest.raises(VoiceGatewayError, match="voice_provider_failed"):
        gateway(timeout).transcribe(wav_bytes())
    assert len(calls) == 1


def test_malformed_or_oversized_asr_response_fails():
    for body in (b"not-json", b" " * 65537):
        with pytest.raises(VoiceGatewayError):
            gateway(lambda _: httpx.Response(200, content=body)).transcribe(wav_bytes())


def test_tts_stream_is_valid_pcm_wav_without_signed_url_download():
    pcm = b"\x01\x00" * 100
    calls = []

    def respond(request):
        calls.append(str(request.url))
        assert request.headers["x-dashscope-sse"] == "enable"
        assert json.loads(request.content)["input"]["text"] == "你好"
        return httpx.Response(200, headers={"content-type": "text/event-stream"},
                              text=event(pcm[:100]) + event(pcm[100:], "stop"))

    audio = gateway(respond).synthesize("你好")
    with wave.open(io.BytesIO(audio)) as wav:
        assert (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) == (24000, 1, 2)
        assert wav.readframes(wav.getnframes()) == pcm
    assert len(calls) == 1


@pytest.mark.parametrize("text", ["", "  ", "字" * 73])
def test_tts_input_is_bounded_before_network(text):
    with pytest.raises(VoiceGatewayError, match="voice_input_invalid"):
        gateway(fail_network).synthesize(text)


@pytest.mark.parametrize("body", [event(), event(b"\x00\x00"), event(b"\x00", "stop"),
    "data: not-json\n\n", event(b"\x00\x00" * (24000 * 16), "stop")])
def test_empty_incomplete_or_oversized_tts_is_not_success(body):
    with pytest.raises(VoiceGatewayError):
        gateway(lambda _: httpx.Response(200, text=body)).synthesize("你好")


@pytest.mark.parametrize("operation", ["asr", "tts"])
def test_deeply_nested_provider_json_returns_safe_error(operation):
    nested = "[" * 1100 + "0" + "]" * 1100
    body = nested if operation == "asr" else "data: " + nested + "\n\n"
    voice = gateway(lambda _: httpx.Response(200, text=body))
    with pytest.raises(VoiceGatewayError):
        if operation == "asr":
            voice.transcribe(wav_bytes())
        else:
            voice.synthesize("你好")


@pytest.mark.parametrize("operation", ["asr", "tts"])
def test_trickle_response_is_stopped_before_whole_body_buffering(operation):
    clock = [0.0]
    yields = []
    body = (json.dumps({"choices": [{"message": {"content": "面食"}}]}).encode()
            if operation == "asr" else (event(b"\x00\x00" * 100, "stop")).encode())

    class Trickle(httpx.SyncByteStream):
        def __iter__(self):
            for byte in body:
                clock[0] += 1.0
                yields.append(byte)
                yield bytes([byte])

    voice = gateway(lambda _: httpx.Response(200, stream=Trickle()))
    with patch("living_canvas_backend.voice_gateway.time.monotonic", side_effect=lambda: clock[0]):
        with pytest.raises(VoiceGatewayError):
            if operation == "asr":
                voice.transcribe(wav_bytes())
            else:
                voice.synthesize("你好")
    assert len(yields) <= 26


@pytest.mark.parametrize("operation", ["asr", "tts"])
@pytest.mark.parametrize("sse", [False, True])
def test_account_unavailable_is_distinguished_without_leaking_provider_body(operation, sse):
    error = json.dumps({"code": "Arrearage", "message": "synthetic-key PRIVATE_DETAILS"})
    body = "event:error\ndata:" + error + "\n\n" if sse else error
    voice = gateway(lambda _: httpx.Response(400, text=body))
    with pytest.raises(VoiceGatewayError, match="^voice_account_unavailable$"):
        if operation == "asr":
            voice.transcribe(wav_bytes())
        else:
            voice.synthesize("你好")
