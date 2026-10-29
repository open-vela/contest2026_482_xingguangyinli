import pytest


ROLE_IDS = {"beagle", "cat", "pigeon", "pig", "alien"}


def create_session(client):
    response = client.post("/v1/session", json={"device_id": "gemini-s1"})
    assert response.status_code == 201
    return response.json()["session_id"]


@pytest.mark.parametrize(
    ("choice", "transcript", "expected_novelty"),
    [
        ("takeout", "想吃热乎的米饭，预算四十元", "balanced"),
        ("mystery", "给我一个稳妥但有惊喜的盲盒", "exploratory"),
        ("home", "今天在家做饭，简单一点", "familiar"),
    ],
)
def test_dialogue_returns_typed_recommendation_and_five_real_roles(
    client, choice, transcript, expected_novelty
):
    session_id = create_session(client)
    response = client.post(
        "/v1/dialogue",
        json={
            "session_id": session_id,
            "choice": choice,
            "transcript": transcript,
        },
    )

    assert response.status_code == 200
    body = response.json()
    assert body["session_id"] == session_id
    assert body["state"] == "candidate"
    assert body["choice"] == choice
    assert body["soft_preferences"]["novelty"] == expected_novelty
    assert body["recommendation"]["candidate"]["name"]
    assert body["recommendation"]["reason"]
    assert body["spoken_reply"]
    assert body["requires_handoff"] is (choice != "home")
    assert {item["role_id"] for item in body["role_contributions"]} == ROLE_IDS
    assert all(item["summary"] for item in body["role_contributions"])


def test_dialogue_rejects_blank_or_unknown_choice(client):
    session_id = create_session(client)
    blank = client.post(
        "/v1/dialogue",
        json={"session_id": session_id, "choice": "takeout", "transcript": " "},
    )
    unknown = client.post(
        "/v1/dialogue",
        json={"session_id": session_id, "choice": "anything", "transcript": "吃饭"},
    )

    assert blank.status_code == 422
    assert unknown.status_code == 422


def test_dialogue_does_not_claim_order_or_payment(client):
    session_id = create_session(client)
    response = client.post(
        "/v1/dialogue",
        json={
            "session_id": session_id,
            "choice": "takeout",
            "transcript": "帮我选一个晚餐",
        },
    )

    assert response.status_code == 200
    body = response.json()
    combined = body["spoken_reply"] + " " + " ".join(
        item["summary"] for item in body["role_contributions"]
    )
    assert "已经下单" not in combined
    assert "已经付款" not in combined
    assert "http://" not in combined
    assert "https://" not in combined


def test_refusal_changes_dish_preserves_session_constraints_and_does_not_confirm(client):
    session_id = create_session(client)
    initial = client.post("/v1/dialogue", json={
        "session_id": session_id, "choice": "takeout",
        "transcript": "预算四十元，花生过敏，想吃热乎的米饭",
    }).json()
    old_id = initial["recommendation"]["candidate"]["candidate_id"]
    response = client.post("/v1/dialogue", json={
        "session_id": session_id, "choice": "takeout",
        "transcript": "我不爱吃这个，换一个", "reject_current": True,
    })
    assert response.status_code == 200
    result = response.json()
    assert result["session_id"] == session_id and result["state"] == "candidate"
    assert result["recommendation"]["candidate"]["candidate_id"] != old_id
    assert result["hard_constraints"]["max_total_price_cny"] == 40
    assert "花生" in result["hard_constraints"]["allergens"]
    assert "米饭" in result["soft_preferences"]["preferred_tags"]
    assert "热" in result["soft_preferences"]["temperatures"]
    session = client.get("/v1/session/" + session_id).json()
    assert session["rejected_candidate_ids"] == [old_id]
    assert "external_url" not in session["metadata"]
    assert client.post("/v1/confirm", json={
        "session_id": session_id, "platform": "eleme",
    }).status_code == 409


def test_followup_explicit_budget_overrides_previous_but_unmentioned_allergy_remains(client):
    session_id = create_session(client)
    client.post("/v1/dialogue", json={"session_id": session_id, "choice": "takeout",
                "transcript": "四十元，花生过敏，想吃米饭"})
    result = client.post("/v1/dialogue", json={"session_id": session_id,
        "choice": "takeout", "transcript": "预算改成五十元，不想吃米饭了，想吃面食"}).json()
    assert result["hard_constraints"]["max_total_price_cny"] == 50
    assert result["hard_constraints"]["allergens"] == ["花生"]
    assert "米饭" not in result["soft_preferences"]["preferred_tags"]
    assert "面食" in result["soft_preferences"]["preferred_tags"]


def test_refusal_requires_existing_candidate_and_cannot_reopen_cancelled_session(client):
    session_id = create_session(client)
    rejected = client.post("/v1/dialogue", json={"session_id": session_id,
        "choice": "takeout", "transcript": "换一个", "reject_current": True})
    assert rejected.status_code == 409
    client.post("/v1/device/event", json={"session_id": session_id, "event": "cancel"})
    assert client.post("/v1/dialogue", json={"session_id": session_id,
        "choice": "takeout", "transcript": "帮我点外卖"}).status_code == 409


def test_repeated_refusals_never_recycle_rejected_dishes_or_fabricate_result(client):
    session_id = create_session(client)
    body = {"session_id": session_id, "choice": "takeout", "transcript": "帮我点外卖"}
    response = client.post("/v1/dialogue", json=body)
    seen = set()
    for _ in range(30):
        if response.status_code == 409:
            assert response.json()["detail"] == "no_safe_candidate"
            break
        assert response.status_code == 200
        candidate = response.json()["recommendation"]["candidate"]["candidate_id"]
        assert candidate not in seen
        seen.add(candidate)
        response = client.post("/v1/dialogue", json={**body, "transcript": "换一个",
                                                   "reject_current": True})
    else:
        pytest.fail("bounded catalog must become exhausted rather than repeat dishes")
    session = client.get("/v1/session/" + session_id).json()
    assert session["recommendation"] is None and session["state"] == "created"
    assert set(session["rejected_candidate_ids"]) == seen
