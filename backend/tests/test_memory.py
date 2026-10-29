from test_handoff import prepare_candidate

import json
import httpx
from fastapi.testclient import TestClient
from living_canvas_backend.app import create_app
from living_canvas_backend.memory import MemoryStore
from living_canvas_backend.model_gateway import ModelGateway
from test_api_flow import valid_input


TEST_OWNER_DEVICE_TOKEN = "lc-owner-test-device-token-0123456789abcdef"


def authenticated_client(app):
    return TestClient(
        app,
        headers={"Authorization": "Bearer " + TEST_OWNER_DEVICE_TOKEN},
    )


def hand_off(client):
    session_id = prepare_candidate(client)
    client.post(
        "/v1/device/event",
        json={"session_id": session_id, "event": "accept_candidate"},
    )
    client.post(
        "/v1/confirm", json={"session_id": session_id, "platform": "eleme"}
    )
    return session_id


def test_feedback_requires_a_confirmed_handoff(client):
    session_id = prepare_candidate(client)
    response = client.post(
        "/v1/feedback",
        json={"session_id": session_id, "liked": True, "note": "好吃"},
    )
    assert response.status_code == 409
    assert response.json()["detail"] == "confirmed_choice_required"


def test_confirmed_feedback_can_be_listed(client):
    session_id = hand_off(client)
    saved = client.post(
        "/v1/feedback",
        json={"session_id": session_id, "liked": True, "note": "下次还想吃"},
    )
    assert saved.status_code == 201

    memory = client.get("/v1/memory").json()
    assert memory["feedback"][0]["candidate_id"]
    assert memory["feedback"][0]["liked"] is True
    assert memory["feedback"][0]["note"] == "下次还想吃"


def test_temporary_preferences_are_not_promoted_automatically(client):
    session_id = hand_off(client)
    client.post(
        "/v1/feedback",
        json={
            "session_id": session_id,
            "liked": True,
            "temporary_preferences": ["今天少辣"],
        },
    )
    assert client.get("/v1/memory").json()["remembered_preferences"] == []


def test_explicit_preferences_can_be_remembered(client):
    session_id = hand_off(client)
    client.post(
        "/v1/feedback",
        json={
            "session_id": session_id,
            "liked": True,
            "remember_preferences": ["偏爱米饭"],
        },
    )
    assert client.get("/v1/memory").json()["remembered_preferences"] == [
        "偏爱米饭"
    ]


def test_all_profile_memory_can_be_deleted(client):
    session_id = hand_off(client)
    client.post(
        "/v1/feedback",
        json={
            "session_id": session_id,
            "liked": False,
            "remember_preferences": ["不喜欢太甜"],
        },
    )
    deleted = client.delete("/v1/memory")
    assert deleted.status_code == 200
    assert deleted.json()["deleted"] is True
    assert client.get("/v1/memory").json() == {
        "feedback": [],
        "remembered_preferences": [],
    }


def remember(client, preference="偏爱面食", device_id=None, note=""):
    sid = client.post("/v1/session", json={"device_id": device_id}).json()["session_id"]
    assert client.post("/v1/input", json=valid_input(sid)).status_code == 200
    assert client.post("/v1/device/event", json={
        "session_id": sid, "event": "accept_candidate",
    }).status_code == 200
    assert client.post("/v1/confirm", json={
        "session_id": sid, "platform": "eleme",
    }).status_code == 200
    assert client.post("/v1/feedback", json={
        "session_id": sid, "liked": True, "note": note,
        "remember_preferences": [preference],
    }).status_code == 201


def next_dialogue(client, device_id=None, transcript="帮我选晚餐", choice="takeout"):
    sid = client.post("/v1/session", json={"device_id": device_id}).json()["session_id"]
    response = client.post("/v1/dialogue", json={
        "session_id": sid, "choice": choice, "transcript": transcript,
    })
    assert response.status_code == 200
    return response.json()


def test_saved_preference_changes_next_dialogue_candidate(client):
    remember(client)
    result = next_dialogue(client)
    assert result["soft_preferences"]["preferred_tags"] == ["面食"]
    assert result["recommendation"]["candidate"]["candidate_id"] == "tomato-beef-noodles"


def test_saved_preference_reaches_structured_input(client):
    remember(client)
    sid = client.post("/v1/session", json={}).json()["session_id"]
    payload = valid_input(sid)
    payload["soft_preferences"] = {}
    result = client.post("/v1/input", json=payload).json()
    assert result["soft_preferences"]["preferred_tags"] == ["面食"]
    assert result["recommendation"]["candidate"]["candidate_id"] == "tomato-beef-noodles"


def test_saved_preference_survives_application_restart(tmp_path):
    remember(authenticated_client(create_app(
        data_dir=tmp_path, voice_device_token=TEST_OWNER_DEVICE_TOKEN
    )), device_id="frame-a")
    restarted = authenticated_client(create_app(
        data_dir=tmp_path, voice_device_token=TEST_OWNER_DEVICE_TOKEN
    ))
    assert next_dialogue(restarted, device_id="frame-a")["soft_preferences"]["preferred_tags"] == ["面食"]


def test_current_preference_overrides_saved_default(client):
    remember(client)
    result = next_dialogue(client, transcript="今天想吃米饭")
    assert result["soft_preferences"]["preferred_tags"] == ["米饭"]
    assert result["recommendation"]["candidate"]["candidate_id"] == "warm-rice-bowl"


def test_deleting_memory_removes_next_recommendation_default(client):
    remember(client)
    assert next_dialogue(client)["soft_preferences"]["preferred_tags"] == ["面食"]
    assert client.delete("/v1/memory").status_code == 200
    assert next_dialogue(client)["soft_preferences"]["preferred_tags"] == []


def test_saved_preferences_do_not_cross_devices(client):
    remember(client, device_id="frame-a")
    assert next_dialogue(client, device_id="frame-a")["soft_preferences"]["preferred_tags"] == ["面食"]
    assert next_dialogue(client, device_id="frame-b")["soft_preferences"]["preferred_tags"] == []
    assert next_dialogue(client)["soft_preferences"]["preferred_tags"] == []
    assert client.get("/v1/memory", params={"device_id": "frame-a"}).json()["remembered_preferences"] == ["偏爱面食"]
    assert client.get("/v1/memory", params={"device_id": "frame-b"}).json()["feedback"] == []


def test_delete_is_scoped_to_requested_device(client):
    remember(client, device_id="frame-a")
    remember(client, preference="偏爱米饭", device_id="frame-b")
    assert client.delete("/v1/memory", params={"device_id": "frame-a"}).status_code == 200
    assert next_dialogue(client, device_id="frame-a")["soft_preferences"]["preferred_tags"] == []
    assert next_dialogue(client, device_id="frame-b")["soft_preferences"]["preferred_tags"] == ["米饭"]


def test_memory_cannot_override_current_allergen_or_budget(client):
    remember(client, preference="偏爱川菜")
    sid = client.post("/v1/session", json={}).json()["session_id"]
    payload = valid_input(sid)
    payload["soft_preferences"] = {}
    payload["hard_constraints"]["allergens"] = ["花生"]
    payload["hard_constraints"]["max_total_price_cny"] = 40
    result = client.post("/v1/input", json=payload).json()
    assert result["soft_preferences"]["cuisines"] == ["川菜"]
    candidate = result["recommendation"]["candidate"]
    assert "花生" not in candidate["allergens"]
    assert candidate["price_cny"] <= 40


def test_notes_and_unrecognized_memory_are_not_model_preferences(client):
    remember(client, preference="不要面食", note="偏爱米饭")
    result = next_dialogue(client)
    assert result["soft_preferences"]["preferred_tags"] == []
    assert client.get("/v1/memory").json()["remembered_preferences"] == ["不要面食"]


def test_mystery_mode_keeps_saved_food_preference(client):
    remember(client)
    result = next_dialogue(client, choice="mystery")
    assert result["soft_preferences"]["preferred_tags"] == ["探索", "盲盒", "面食"]
    assert result["soft_preferences"]["novelty"] == "exploratory"


def test_current_named_cuisine_overrides_saved_cuisine(client):
    remember(client, preference="偏爱川菜")
    result = next_dialogue(client, transcript="今天想吃西式，帮我选晚餐")
    assert result["soft_preferences"]["cuisines"] == ["西式"]
    assert result["recommendation"]["candidate"]["cuisine"] == "西式"


def test_current_rejected_cuisine_is_not_restored_or_recommended(client):
    remember(client, preference="偏爱川菜")
    result = next_dialogue(client, transcript="今天不想吃川菜，帮我选晚餐")
    assert "川菜" in result["hard_constraints"]["dislikes"]
    assert "川菜" not in result["soft_preferences"]["cuisines"]
    assert result["recommendation"]["candidate"]["cuisine"] != "川菜"


def test_current_rejected_food_tag_is_not_restored_or_recommended(client):
    remember(client)
    result = next_dialogue(client, transcript="今天不要面食，帮我选晚餐")
    assert "面食" in result["hard_constraints"]["dislikes"]
    assert "面食" not in result["soft_preferences"]["preferred_tags"]
    assert "面食" not in result["recommendation"]["candidate"]["tags"]


def test_negated_food_list_does_not_create_positive_preferences(client):
    remember(client)
    result = next_dialogue(client, transcript="不要米饭和面食，今天吃轻食")
    assert set(result["hard_constraints"]["dislikes"]) == {"米饭", "面食"}
    assert result["soft_preferences"]["preferred_tags"] == ["轻食"]
    assert not {"米饭", "面食"}.intersection(result["recommendation"]["candidate"]["tags"])


def test_postposed_cuisine_rejection_does_not_restore_memory(client):
    remember(client, preference="偏爱川菜")
    result = next_dialogue(client, transcript="川菜和西式都不要，帮我选晚餐")
    assert set(result["hard_constraints"]["dislikes"]) == {"川菜", "西式"}
    assert result["soft_preferences"]["cuisines"] == []
    assert result["recommendation"]["candidate"]["cuisine"] not in {"川菜", "西式"}


def test_rejection_does_not_spread_past_clause_boundary(client):
    remember(client, preference="偏爱米饭")
    result = next_dialogue(client, transcript="不要米饭，想吃面食")
    assert result["hard_constraints"]["dislikes"] == ["米饭"]
    assert result["soft_preferences"]["preferred_tags"] == ["面食"]


def test_current_cuisine_missing_from_catalog_still_overrides_saved_default(client):
    remember(client, preference="偏爱川菜")
    result = next_dialogue(client, transcript="今天想吃粤菜，帮我选晚餐")
    assert result["soft_preferences"]["cuisines"] == ["粤菜"]


def test_effective_memory_reaches_model_without_raw_notes(tmp_path):
    prompts = []

    def respond(request):
        payload = json.loads(request.content)
        prompt = json.loads(payload["messages"][1]["content"])
        prompts.append(prompt)
        content = json.dumps({"candidate_id": prompt["candidates"][0]["candidate_id"], "reason": "测试推荐"})
        return httpx.Response(200, json={"choices": [{"message": {"content": content}}]})

    model = ModelGateway("https://api.xiaomimimo.com/v1", "mimo-v2.6-flash", "synthetic-key",
                         client=httpx.Client(transport=httpx.MockTransport(respond)))
    client = authenticated_client(create_app(
        data_dir=tmp_path,
        model_gateway=model,
        voice_device_token=TEST_OWNER_DEVICE_TOKEN,
    ))
    remember(client, note="PRIVATE_NOTE_MUST_NOT_ENTER_MODEL")
    result = next_dialogue(client)
    assert result["recommendation"]["source"] == "model"
    assert prompts[-1]["preferences"]["preferred_tags"] == ["面食"]
    sid = client.post("/v1/session", json={}).json()["session_id"]
    payload = valid_input(sid)
    payload["soft_preferences"] = {}
    result = client.post("/v1/input", json=payload).json()
    assert result["recommendation"]["source"] == "model"
    assert prompts[-1]["preferences"]["preferred_tags"] == ["面食"]
    assert "PRIVATE_NOTE_MUST_NOT_ENTER_MODEL" not in json.dumps(prompts)


def test_rejected_cuisine_is_removed_before_model_call(tmp_path):
    prompts = []

    def respond(request):
        prompt = json.loads(json.loads(request.content)["messages"][1]["content"])
        prompts.append(prompt)
        content = json.dumps({"candidate_id": prompt["candidates"][0]["candidate_id"], "reason": "测试推荐"})
        return httpx.Response(200, json={"choices": [{"message": {"content": content}}]})

    model = ModelGateway("https://api.xiaomimimo.com/v1", "mimo-v2.6-flash", "synthetic-key",
                         client=httpx.Client(transport=httpx.MockTransport(respond)))
    client = authenticated_client(create_app(
        data_dir=tmp_path,
        model_gateway=model,
        voice_device_token=TEST_OWNER_DEVICE_TOKEN,
    ))
    remember(client, preference="偏爱川菜")
    result = next_dialogue(client, transcript="今天不想吃川菜")
    assert result["recommendation"]["source"] == "model"
    assert "川菜" not in prompts[-1]["preferences"]["cuisines"]
    assert all(candidate["cuisine"] != "川菜" for candidate in prompts[-1]["candidates"])


def test_legacy_database_is_preserved_without_assigning_it_to_a_device(tmp_path):
    import sqlite3

    path = tmp_path / "living_canvas_memory.sqlite3"
    with sqlite3.connect(path) as connection:
        connection.executescript("""
            CREATE TABLE feedback(id INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id TEXT NOT NULL, candidate_id TEXT NOT NULL,
                liked INTEGER NOT NULL, note TEXT, created_at TEXT NOT NULL);
            CREATE TABLE remembered_preferences(value TEXT PRIMARY KEY,
                created_at TEXT NOT NULL);
            INSERT INTO feedback VALUES(1, 'meal_legacy', 'rice', 1, 'legacy', '2026-01-01');
            INSERT INTO remembered_preferences VALUES('偏爱面食', '2026-01-01');
        """)
    memory = MemoryStore(tmp_path)
    assert memory.list_all()["remembered_preferences"] == ["偏爱面食"]
    assert memory.list_all()["feedback"][0]["note"] == "legacy"
    assert memory.list_all("frame-a") == {"feedback": [], "remembered_preferences": []}
    assert memory.list_all()["feedback"][0]["session_id"] == "meal_legacy"
