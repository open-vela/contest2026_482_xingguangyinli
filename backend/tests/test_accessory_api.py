from fastapi.testclient import TestClient

from living_canvas_backend.app import create_app


TOKEN = "synthetic-accessory-token-at-least-32-characters"
AUTH = {"Authorization": "Bearer " + TOKEN}


def configured(tmp_path):
    return TestClient(create_app(
        data_dir=tmp_path,
        accessory_device_token=TOKEN,
    ))


def event(device_id="cat_a", session_id="boot-a", sequence=1,
          kind="checkin", value=True):
    return {
        "device_id": device_id,
        "session_id": session_id,
        "sequence": sequence,
        "event": kind,
        "value": value,
    }


def test_accessory_routes_are_disabled_without_explicit_token(tmp_path):
    client = TestClient(create_app(data_dir=tmp_path, accessory_device_token=""))

    posted = client.post("/v1/accessories/events", json=event(), headers=AUTH)
    polled = client.get("/v1/accessories/events", headers=AUTH)

    assert posted.status_code == 503
    assert polled.status_code == 503
    assert posted.json()["detail"] == "accessories_not_configured"


def test_accessory_routes_require_their_own_bearer_token(tmp_path):
    client = configured(tmp_path)

    for headers in ({}, {"Authorization": "Bearer wrong"},
                    {"Authorization": "Basic bad"}):
        assert client.post(
            "/v1/accessories/events", json=event(), headers=headers
        ).status_code == 401
        assert client.get(
            "/v1/accessories/events", headers=headers
        ).status_code == 401

    assert client.get("/v1/accessories/events", headers=AUTH).json()["events"] == []


def test_cat_checkin_can_be_polled_with_a_server_cursor(tmp_path):
    client = configured(tmp_path)

    accepted = client.post(
        "/v1/accessories/events", json=event(), headers=AUTH
    )
    assert accepted.status_code == 202
    assert accepted.content == (
        b'{"cursor":1,"device_id":"cat_a","session_id":"boot-a",'
        b'"sequence":1,"event":"checkin","value":true}'
    )
    assert accepted.json() == {
        "cursor": 1,
        "device_id": "cat_a",
        "session_id": "boot-a",
        "sequence": 1,
        "event": "checkin",
        "value": True,
    }

    first = client.get("/v1/accessories/events?after=0", headers=AUTH)
    assert first.status_code == 200
    assert first.json() == {
        "events": [accepted.json()],
        "next_cursor": 1,
        "truncated": False,
    }
    assert client.get(
        "/v1/accessories/events?after=1", headers=AUTH
    ).json()["events"] == []


def test_sequence_replay_is_rejected_but_new_boot_session_is_accepted(tmp_path):
    client = configured(tmp_path)
    assert client.post(
        "/v1/accessories/events", json=event(sequence=4), headers=AUTH
    ).status_code == 202

    for sequence in (4, 3):
        replay = client.post(
            "/v1/accessories/events", json=event(sequence=sequence), headers=AUTH
        )
        assert replay.status_code == 409
        assert replay.content == b'{"detail":"accessory_event_replayed"}'
        assert replay.json()["detail"] == "accessory_event_replayed"

    restarted = client.post(
        "/v1/accessories/events",
        json=event(session_id="boot-b", sequence=1),
        headers=AUTH,
    )
    assert restarted.status_code == 202
    assert restarted.json()["cursor"] == 2


def test_device_event_pairs_and_boolean_values_are_strict(tmp_path):
    client = configured(tmp_path)
    invalid = (
        event(device_id="cat_a", kind="presence"),
        event(device_id="mmwave_bridge", kind="checkin"),
        event(device_id="cat_a", value=False),
        event(device_id="cat_c"),
        event(value="yes"),
        event(sequence="2"),
        event(session_id="x" * 24),
    )

    for payload in invalid:
        response = client.post(
            "/v1/accessories/events", json=payload, headers=AUTH
        )
        assert response.status_code == 422

    present = client.post(
        "/v1/accessories/events",
        json=event(device_id="mmwave_bridge", kind="presence", value=True),
        headers=AUTH,
    )
    absent = client.post(
        "/v1/accessories/events",
        json=event(device_id="mmwave_bridge", sequence=2,
                   kind="presence", value=False),
        headers=AUTH,
    )
    assert present.status_code == 202
    assert absent.status_code == 202


def test_poll_limit_and_bounded_history_report_truncation(tmp_path):
    client = configured(tmp_path)
    for sequence in range(1, 258):
        assert client.post(
            "/v1/accessories/events",
            json=event(sequence=sequence),
            headers=AUTH,
        ).status_code == 202

    first_page = client.get(
        "/v1/accessories/events?after=0&limit=2", headers=AUTH
    ).json()
    assert first_page["truncated"] is True
    assert [item["cursor"] for item in first_page["events"]] == [2, 3]
    assert first_page["next_cursor"] == 3

    second_page = client.get(
        "/v1/accessories/events?after=3&limit=2", headers=AUTH
    ).json()
    assert second_page["truncated"] is False
    assert [item["cursor"] for item in second_page["events"]] == [4, 5]


def test_poll_rejects_cursor_ahead_of_server(tmp_path):
    client = configured(tmp_path)
    response = client.get("/v1/accessories/events?after=1", headers=AUTH)
    assert response.status_code == 409
    assert response.json()["detail"] == "accessory_cursor_ahead"
