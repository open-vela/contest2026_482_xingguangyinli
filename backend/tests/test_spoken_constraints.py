"""Real speech-text parsing and API filtering; no microphone/provider calls."""
import pytest

from living_canvas_backend.roles import build_dialogue_inputs
from living_canvas_backend.schema import HardConstraints, MealChoice


@pytest.mark.parametrize(("speech", "amount"), [
    ("预算八元", 8),
    ("预算二十五元", 25),
    ("预算七十元", 70),
    ("预算一百二十元", 120),
    ("预算20.5元", 20.5),
    ("预算8元", 8),
    ("预算25元到40元", 40),
    ("预算二十五到四十元", 40),
    ("预算零点五元", 0.5),
    ("预算２０元", 20),
    ("预算30元，不对，最多20元", 20),
    ("预算20元，改成30元", 30),
    ("预算20元，运费5元", 20),
])
def test_budget_uses_spoken_amount_without_raising_minimum(speech, amount):
    constraints, _, _ = build_dialogue_inputs(MealChoice.TAKEOUT, speech)
    assert constraints.max_total_price_cny == amount


@pytest.mark.parametrize(("speech", "minutes"), [
    ("二十分钟内送到", 20),
    ("15分钟内吃到", 15),
    ("等不了太久，半小时内", 30),
    ("最多一刻钟", 15),
    ("四十五分钟内送到", 45),
    ("一个小时内送到", 60),
    ("40分钟太久，20分钟内送到", 20),
    ("最多半刻钟送到", 7),
    ("半个小时内送到", 30),
    ("一个小时太久，半小时内送到", 30),
    ("30分钟太久，最多半刻钟内送到", 7),
])
def test_delivery_limit_uses_spoken_time(speech, minutes):
    constraints, _, _ = build_dialogue_inputs(
        MealChoice.TAKEOUT, speech,
        previous_constraints=HardConstraints(max_delivery_minutes=60),
    )
    assert constraints.max_delivery_minutes == minutes


def test_followup_keeps_unmentioned_budget_and_time():
    constraints, _, _ = build_dialogue_inputs(
        MealChoice.TAKEOUT, "这个不爱吃，换一个",
        previous_constraints=HardConstraints(
            max_total_price_cny=25, max_delivery_minutes=20,
            allergens=["花生"],
        ),
    )
    assert constraints.max_total_price_cny == 25
    assert constraints.max_delivery_minutes == 20
    assert constraints.allergens == ["花生"]


def test_api_does_not_recommend_over_spoken_delivery_deadline(client):
    sid = client.post("/v1/session", json={}).json()["session_id"]
    result = client.post("/v1/dialogue", json={
        "session_id": sid, "choice": "takeout", "transcript": "十分钟内吃到",
    })
    if result.status_code == 200:
        body = result.json()
        assert body["hard_constraints"]["max_delivery_minutes"] == 10
        assert body["recommendation"]["candidate"]["delivery_minutes"] <= 10
    else:
        assert result.status_code == 409
        assert result.json()["detail"] == "no_safe_candidate"


def test_api_does_not_increase_small_spoken_budget(client):
    sid = client.post("/v1/session", json={}).json()["session_id"]
    result = client.post("/v1/dialogue", json={
        "session_id": sid, "choice": "takeout", "transcript": "预算八元",
    })
    if result.status_code == 200:
        body = result.json()
        assert body["hard_constraints"]["max_total_price_cny"] == 8
        assert body["recommendation"]["candidate"]["price_cny"] <= 8
    else:
        assert result.status_code == 409
        assert result.json()["detail"] == "no_safe_candidate"


@pytest.mark.parametrize("speech", [
    "预算0元", "预算-5元", "零分钟内送到", "预算负五元", "预算负5元",
    "预算一千元", "预算20", "预算20元，30元", "最多零点五分钟内送到",
    "预算50元，改成10", "预算50元，预算10",
])
def test_nonpositive_spoken_limits_require_correction_not_default_budget(client, speech):
    sid = client.post("/v1/session", json={}).json()["session_id"]
    response = client.post("/v1/dialogue", json={
        "session_id": sid, "choice": "takeout", "transcript": speech,
    })
    assert response.status_code == 422
    assert response.json()["detail"] == "invalid_spoken_constraints"
