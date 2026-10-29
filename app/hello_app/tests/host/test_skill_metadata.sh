#!/usr/bin/env bash
set -euo pipefail

app_root="$(cd "$(dirname "$0")/../.." && pwd)"
skill="$app_root/skills/dinner-assistant.md"
demo="$app_root/../../docs/SKILL_DEMO.md"

test -f "$skill"
grep -q '^# Dinner Assistant$' "$skill"
grep -q '^## When to use$' "$skill"
grep -q '^## How to use$' "$skill"
grep -q '^## Example$' "$skill"
grep -q '/data/agent/skills/dinner-assistant.md' "$demo"

python3 - "$skill" <<'PY'
import json
import pathlib
import re
import sys

text = pathlib.Path(sys.argv[1]).read_text()
blocks = re.findall(r"^```json\n(.*?)\n```$", text, re.M | re.S)
assert blocks, "missing machine-readable runtime output contract"
schema = json.loads(blocks[0])
assert schema["type"] == "object"
assert schema["additionalProperties"] is False
fields = schema["properties"]
assert set(schema["required"]) == set(fields) == {
    "reply", "candidates", "candidate_count", "next_state", "actions"
}
assert fields["next_state"]["enum"] == ["LISTENING", "RECOMMENDATION", "ERROR"]
assert fields["actions"]["type"] == "array"
assert fields["actions"]["uniqueItems"] is True
assert fields["actions"]["items"]["enum"] == ["SHOW_STATE", "SHOW_QR"]
assert fields["reply"]["type"] == "string"
assert fields["candidate_count"] == {"type": "integer", "minimum": 0, "maximum": 3}
assert fields["candidates"]["type"] == "array"
assert fields["candidates"]["maxItems"] == 3
candidate = fields["candidates"]["items"]
assert candidate["type"] == "object"
assert candidate["additionalProperties"] is False
assert set(candidate["required"]) == set(candidate["properties"]) == {
    "name", "reason", "qr_target", "price_cents", "allergen_flags",
    "spicy", "requires_cooking"
}
for name in ("name", "reason", "qr_target"):
    assert candidate["properties"][name]["type"] == "string"
for name in ("price_cents", "allergen_flags"):
    assert candidate["properties"][name] == {"type": "integer", "minimum": 0}
for name in ("spicy", "requires_cooking"):
    assert candidate["properties"][name]["type"] == "boolean"

# These semantic rules supplement JSON Schema without requiring a validator dependency.
prose = " ".join(text.split())
for rule in (
    "candidate_count must equal the length of candidates.",
    "With zero candidates, next_state must be LISTENING or ERROR; omit SHOW_QR.",
    "With one to three candidates, next_state must be RECOMMENDATION.",
    "Reject SET_LIGHT, SAVE_CONFIRMED_MEMORY, shell or device control, credentials, "
    "payment, and unknown actions at the Skill boundary.",
    "Persistent memory and physical actions require explicit application confirmation "
    "and must never be emitted by this Skill.",
    "Use SHOW_QR only for an application-supplied, verified qr_target.",
    "UTF-8 byte limits before the C NUL terminator: reply 159, name 47, reason 95, "
    "qr_target 127; no embedded NUL characters.",
):
    assert rule in prose, f"missing runtime rule: {rule}"
assert "lc_dinner_result_t" in text
assert "lc_dinner_validate_result" in text

example = json.loads(blocks[1])
assert set(example) == set(fields)
assert example["next_state"] == "LISTENING"
assert example["actions"] == ["SHOW_STATE"]
assert example["candidate_count"] == 0 and example["candidates"] == []
assert isinstance(example["reply"], str) and example["reply"]
assert len(example["reply"].encode("utf-8")) <= 159
print("PASS: dinner assistant runtime contract")
PY

echo 'PASS: dinner assistant skill metadata'
