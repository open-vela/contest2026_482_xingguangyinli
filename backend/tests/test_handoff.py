from urllib.parse import parse_qs, urlparse
from concurrent.futures import ThreadPoolExecutor
from threading import Barrier

import pytest

import living_canvas_backend.app as app_module
from living_canvas_backend.handoff import build_platform_search_url, is_allowed_url
from test_api_flow import create_session, valid_input


def prepare_candidate(client):
    session_id = create_session(client)
    assert client.post("/v1/input", json=valid_input(session_id)).status_code == 200
    return session_id


def test_accept_then_confirm_creates_phone_handoff(client):
    session_id = prepare_candidate(client)
    accepted = client.post(
        "/v1/device/event",
        json={"session_id": session_id, "event": "accept_candidate"},
    )
    assert accepted.status_code == 200
    assert accepted.json()["state"] == "confirming"

    confirmed = client.post(
        "/v1/confirm", json={"session_id": session_id, "platform": "eleme"}
    )
    body = confirmed.json()
    assert confirmed.status_code == 200
    assert body["state"] == "handed_off"
    assert is_allowed_url(body["external_url"])
    assert body["compact_path"].startswith("/c/")
    assert body["requires_user_payment"] is True


def test_confirm_before_device_accept_is_rejected(client):
    session_id = prepare_candidate(client)
    response = client.post(
        "/v1/confirm", json={"session_id": session_id, "platform": "meituan"}
    )
    assert response.status_code == 409
    assert response.json()["detail"] == "confirmation_required"


def test_platform_url_is_allowlisted_and_query_is_encoded():
    url = build_platform_search_url("eleme", "鸡肉 盖饭&汤")
    parsed = urlparse(url)
    assert parsed.hostname == "h5.ele.me"
    assert parsed.path == "/search/"
    assert parse_qs(parsed.query)["keyword"] == ["鸡肉 盖饭&汤"]
    assert is_allowed_url(url)
    assert not is_allowed_url("https://www.ele.me.example.com/search")
    assert not is_allowed_url("https://www.meituan.com/s/?q=盖饭")
    with pytest.raises(ValueError):
        build_platform_search_url("meituan", "盖饭")
    with pytest.raises(ValueError):
        build_platform_search_url("unknown", "盖饭")


def test_confirm_rejects_non_eleme_platform_without_advancing_state(client):
    session_id = prepare_candidate(client)
    assert client.post(
        "/v1/device/event",
        json={"session_id": session_id, "event": "accept_candidate"},
    ).status_code == 200

    rejected = client.post(
        "/v1/confirm", json={"session_id": session_id, "platform": "meituan"}
    )
    assert rejected.status_code == 422
    assert rejected.json()["detail"] == "unsupported_platform"
    session = client.get("/v1/session/" + session_id).json()
    assert session["state"] == "confirming"
    assert "external_url" not in session["metadata"]


def test_compact_link_redirects_to_owned_console(client):
    session_id = create_session(client)
    token = session_id.removeprefix("meal_")
    response = client.get("/c/" + token, follow_redirects=False)
    assert response.status_code == 307
    assert response.headers["location"] == "/console?session=" + session_id
    assert client.get("/c/not-valid", follow_redirects=False).status_code == 404


def test_phone_console_has_living_canvas_roles(client):
    response = client.get("/console")
    assert response.status_code == 200
    assert "Living Canvas" in response.text
    assert "画间" in response.text
    for role in ["比格犬", "美短猫", "鸽子", "小猪", "外星人"]:
        assert role in response.text


def test_phone_console_allows_only_the_real_eleme_handoff_host(client):
    response = client.get("/console")

    assert response.status_code == 200
    assert "h5.ele.me" in response.text
    assert "www.ele.me" not in response.text
    assert "meituan.com" not in response.text


def test_confirmed_compact_qr_goes_directly_to_eleme(client):
    session_id = prepare_candidate(client)
    assert client.post("/v1/device/event", json={
        "session_id": session_id, "event": "accept_candidate"}).status_code == 200
    confirmed = client.post("/v1/confirm", json={
        "session_id": session_id, "platform": "eleme"}).json()
    response = client.get(confirmed["compact_path"], follow_redirects=False)
    assert response.status_code == 307
    assert response.headers["location"] == confirmed["external_url"]
    assert urlparse(response.headers["location"]).hostname == "h5.ele.me"


def test_cancelled_compact_qr_does_not_jump_to_platform(client):
    session_id = prepare_candidate(client)
    client.post("/v1/device/event", json={"session_id": session_id, "event": "cancel"})
    response = client.get("/c/" + session_id.removeprefix("meal_"), follow_redirects=False)
    assert response.headers["location"].startswith("/console?")


def test_handed_off_session_cannot_be_cancelled_or_lose_direct_handoff(client):
    session_id = prepare_candidate(client)
    assert client.post("/v1/device/event", json={
        "session_id": session_id, "event": "accept_candidate"}).status_code == 200
    confirmed = client.post("/v1/confirm", json={
        "session_id": session_id, "platform": "eleme"})
    assert confirmed.status_code == 200

    cancelled = client.post("/v1/device/event", json={
        "session_id": session_id, "event": "cancel"})

    assert cancelled.status_code == 409
    assert cancelled.json()["detail"] == "invalid_state_transition"
    stored = client.get("/v1/session/" + session_id).json()
    assert stored["state"] == "handed_off"
    response = client.get(confirmed.json()["compact_path"], follow_redirects=False)
    assert response.status_code == 307
    assert response.headers["location"] == confirmed.json()["external_url"]


def test_concurrent_confirm_commits_handoff_exactly_once(client, monkeypatch):
    session_id = prepare_candidate(client)
    assert client.post("/v1/device/event", json={
        "session_id": session_id, "event": "accept_candidate"}).status_code == 200
    rendezvous = Barrier(2)
    original = app_module.build_platform_search_url

    def synchronized_build(platform, query):
        rendezvous.wait(timeout=5)
        return original(platform, query)

    monkeypatch.setattr(app_module, "build_platform_search_url", synchronized_build)

    def confirm_once():
        return client.post("/v1/confirm", json={
            "session_id": session_id, "platform": "eleme"})

    with ThreadPoolExecutor(max_workers=2) as executor:
        responses = list(executor.map(lambda _: confirm_once(), range(2)))

    assert sorted(response.status_code for response in responses) == [200, 409]
    rejected = next(response for response in responses if response.status_code == 409)
    assert rejected.json()["detail"] == "confirmation_required"
    assert client.get("/v1/session/" + session_id).json()["state"] == "handed_off"
