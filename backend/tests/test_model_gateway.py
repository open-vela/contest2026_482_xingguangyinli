import json
from unittest.mock import patch

import httpx
import pytest

from living_canvas_backend.model_gateway import ModelGateway
from living_canvas_backend.schema import Candidate, SoftPreferences


def candidate(candidate_id):
    return Candidate(
        candidate_id=candidate_id,
        name=candidate_id,
        cuisine="中式",
        temperature="热",
        price_cny=30,
        delivery_minutes=25,
        channels=["delivery"],
    )


def gateway(handler=None, **overrides):
    client = httpx.Client(transport=httpx.MockTransport(handler)) if handler else None
    values = {
        "base_url": "https://api.xiaomimimo.com/v1",
        "model": "mimo-v2.6-flash",
        "api_key": "secret-value",
        "client": client,
    }
    values.update(overrides)
    return ModelGateway(**values)


def dashscope_gateway(handler=None, **overrides):
    values = {
        "base_url": "https://dashscope.aliyuncs.com/compatible-mode/v1",
        "model": "qwen-flash",
    }
    values.update(overrides)
    return gateway(handler, **values)


def test_disabled_gateway_does_not_call_network():
    model = gateway(base_url="", model="", api_key="", client=None)
    assert model.choose([candidate("rice")], SoftPreferences()) is None


def test_default_client_ignores_ambient_proxy_and_redirect_configuration():
    with patch("living_canvas_backend.model_gateway.httpx.Client") as client:
        gateway(client=None)
    _, kwargs = client.call_args
    assert kwargs["trust_env"] is False
    assert kwargs["follow_redirects"] is False


@pytest.mark.parametrize(
    "base_url",
    [
        "http://api.xiaomimimo.com/v1",
        "https://model.example/v1",
        "https://user@api.xiaomimimo.com/v1",
        "https://api.xiaomimimo.com:443/v1",
        "https://api.xiaomimimo.com/v1/chat",
        "https://api.xiaomimimo.com/v1?target=elsewhere",
        "https://api.xiaomimimo.com/v1#fragment",
    ],
)
def test_untrusted_base_url_is_disabled_without_sending_credentials(base_url):
    requests = []

    def capture(request):
        requests.append(request)
        raise AssertionError("untrusted model endpoint must not receive a request")

    model = gateway(capture, base_url=base_url)
    assert model.enabled is False
    assert model.choose([candidate("rice")], SoftPreferences()) is None
    assert requests == []


def test_canonical_base_url_with_trailing_slash_is_allowed():
    requests = []

    def selected(request):
        requests.append(request)
        content = json.dumps({"candidate_id": "rice", "reason": "符合偏好"})
        return httpx.Response(200, json={"choices": [{"message": {"content": content}}]})

    model = gateway(selected, base_url="https://api.xiaomimimo.com/v1/")
    assert model.enabled is True
    assert model.choose([candidate("rice")], SoftPreferences()).candidate_id == "rice"
    assert str(requests[0].url) == "https://api.xiaomimimo.com/v1/chat/completions"


def test_existing_dashscope_qwen_flash_configuration_is_allowed():
    requests = []

    def selected(request):
        requests.append(request)
        content = json.dumps({"candidate_id": "rice", "reason": "符合偏好"})
        return httpx.Response(200, json={"choices": [{"message": {"content": content}}]})

    model = dashscope_gateway(selected)

    assert model.enabled is True
    assert model.choose([candidate("rice")], SoftPreferences()).candidate_id == "rice"
    assert str(requests[0].url) == (
        "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions"
    )
    payload = json.loads(requests[0].content)
    assert payload["enable_thinking"] is False
    assert "thinking" not in payload


@pytest.mark.parametrize(
    "base_url,model_name",
    [
        ("http://dashscope.aliyuncs.com/compatible-mode/v1", "qwen-flash"),
        ("https://user@dashscope.aliyuncs.com/compatible-mode/v1", "qwen-flash"),
        ("https://dashscope.aliyuncs.com/compatible-mode/v1/chat", "qwen-flash"),
        ("https://dashscope.aliyuncs.com/compatible-mode/v1", "qwen-plus"),
    ],
)
def test_unapproved_dashscope_profile_is_disabled_without_network(
        base_url, model_name):
    requests = []

    def capture(request):
        requests.append(request)
        raise AssertionError("unapproved DashScope profile must not receive a request")

    model = gateway(capture, base_url=base_url, model=model_name)
    assert model.enabled is False
    assert model.choose([candidate("rice")], SoftPreferences()) is None
    assert requests == []


@pytest.mark.parametrize("model_name", ["mimo-v2.6-flash", "mimo-v2.6-pro"])
def test_current_pay_as_you_go_models_are_allowed(model_name):
    requests = []

    def selected(request):
        requests.append(request)
        content = json.dumps({"candidate_id": "rice", "reason": "符合偏好"})
        return httpx.Response(200, json={"choices": [{"message": {"content": content}}]})

    model = gateway(selected, model=model_name)
    assert model.enabled is True
    assert model.choose([candidate("rice")], SoftPreferences()).candidate_id == "rice"
    assert json.loads(requests[0].content)["model"] == model_name


@pytest.mark.parametrize(
    "model_name",
    ["mimo-v2.5", "mimo-v2.5-pro", "decision-model", "mimo-v2.6-pro-ultraspeed"],
)
def test_retired_or_unapproved_model_is_disabled_without_network(model_name):
    requests = []

    def capture(request):
        requests.append(request)
        raise AssertionError("retired or unapproved model must not receive a request")

    model = gateway(capture, model=model_name)
    assert model.enabled is False
    assert model.choose([candidate("rice")], SoftPreferences()) is None
    assert requests == []


@pytest.mark.parametrize("api_key", ["tp-test-key-123456", "ttp-test-key-123456"])
def test_token_plan_key_is_disabled_for_product_backend_without_network(api_key):
    requests = []

    def capture(request):
        requests.append(request)
        raise AssertionError("Token Plan credentials must not be used by product backend")

    model = gateway(capture, api_key=api_key)
    assert model.enabled is False
    assert model.choose([candidate("rice")], SoftPreferences()) is None
    assert requests == []


def test_timeout_falls_back_without_raising():
    def timeout(request):
        raise httpx.ReadTimeout("slow", request=request)

    assert gateway(timeout).choose([candidate("rice")], SoftPreferences()) is None


def test_malformed_model_json_is_rejected():
    def malformed(_request):
        return httpx.Response(
            200, json={"choices": [{"message": {"content": "not-json"}}]}
        )

    assert gateway(malformed).choose([candidate("rice")], SoftPreferences()) is None


def test_candidate_outside_filtered_set_is_rejected():
    def unknown(_request):
        content = json.dumps({"candidate_id": "unsafe", "reason": "guess"})
        return httpx.Response(200, json={"choices": [{"message": {"content": content}}]})

    assert gateway(unknown).choose([candidate("rice")], SoftPreferences()) is None


@pytest.mark.parametrize("reason", ["", "   ", "\n\t"])
def test_empty_or_whitespace_model_reason_is_rejected(reason):
    def empty_reason(_request):
        content = json.dumps({"candidate_id": "rice", "reason": reason})
        return httpx.Response(
            200, json={"choices": [{"message": {"content": content}}]}
        )

    assert gateway(empty_reason).choose(
        [candidate("rice")], SoftPreferences()
    ) is None


def test_successful_structured_selection_returns_candidate_and_reason():
    def selected(request):
        assert request.headers["authorization"] == "Bearer secret-value"
        content = json.dumps({"candidate_id": "rice", "reason": "符合偏好"})
        return httpx.Response(200, json={"choices": [{"message": {"content": content}}]})

    choice = gateway(selected).choose([candidate("rice")], SoftPreferences())
    assert choice.candidate_id == "rice"
    assert choice.reason == "符合偏好"


def test_mimo_request_disables_thinking_and_bounds_output_without_temperature():
    requests = []

    def selected(request):
        requests.append(request)
        content = json.dumps({"candidate_id": "rice", "reason": "符合偏好"})
        return httpx.Response(200, json={"choices": [{"message": {"content": content}}]})

    choice = gateway(selected).choose([candidate("rice")], SoftPreferences())

    assert choice is not None
    payload = json.loads(requests[0].content)
    assert payload["thinking"] == {"type": "disabled"}
    assert payload["max_completion_tokens"] == 256
    assert "temperature" not in payload
    assert "top_p" not in payload
