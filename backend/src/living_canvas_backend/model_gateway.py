import json
import os
from dataclasses import dataclass
from typing import List, Optional

import httpx

from .schema import Candidate, SoftPreferences


MIMO_BASE_URL = "https://api.xiaomimimo.com/v1"
DASHSCOPE_BASE_URL = "https://dashscope.aliyuncs.com/compatible-mode/v1"
# Keep the roadshow path intentionally narrow: these are the two current,
# generally available pay-as-you-go models approved for this product backend.
SUPPORTED_MIMO_MODELS = frozenset(("mimo-v2.6-flash", "mimo-v2.6-pro"))
SUPPORTED_DASHSCOPE_MODELS = frozenset(("qwen-flash",))
TOKEN_PLAN_KEY_PREFIXES = ("tp-", "ttp-")


@dataclass(frozen=True)
class ModelChoice:
    candidate_id: str
    reason: str


class ModelGateway:
    def __init__(
        self,
        base_url: str,
        model: str,
        api_key: str,
        client: Optional[httpx.Client] = None,
        timeout_seconds: float = 4.0,
    ):
        self.base_url = base_url.rstrip("/")
        self.model = model
        self.api_key = api_key
        self.client = client or httpx.Client(
            timeout=timeout_seconds,
            follow_redirects=False,
            trust_env=False,
        )

    @classmethod
    def from_env(cls):
        return cls(
            base_url=os.environ.get("LIVING_CANVAS_MODEL_BASE_URL", ""),
            model=os.environ.get("LIVING_CANVAS_MODEL_NAME", ""),
            api_key=os.environ.get("LIVING_CANVAS_MODEL_API_KEY", ""),
        )

    @property
    def enabled(self) -> bool:
        supported_models = {
            MIMO_BASE_URL: SUPPORTED_MIMO_MODELS,
            DASHSCOPE_BASE_URL: SUPPORTED_DASHSCOPE_MODELS,
        }.get(self.base_url)
        return bool(
            supported_models
            and self.model in supported_models
            and self.api_key
            and not self.api_key.startswith(TOKEN_PLAN_KEY_PREFIXES)
        )

    def choose(
        self,
        candidates: List[Candidate],
        preferences: SoftPreferences,
    ) -> Optional[ModelChoice]:
        if not self.enabled or not candidates:
            return None

        candidate_ids = {item.candidate_id for item in candidates}
        compact_candidates = [
            {
                "candidate_id": item.candidate_id,
                "name": item.name,
                "cuisine": item.cuisine,
                "temperature": item.temperature,
                "tags": item.tags,
            }
            for item in candidates
        ]
        prompt = {
            "task": "Choose exactly one candidate ID and give a short reason.",
            "preferences": preferences.model_dump(),
            "candidates": compact_candidates,
            "response_schema": {"candidate_id": "string", "reason": "string"},
        }
        request_body = {
            "model": self.model,
            "messages": [
                {
                    "role": "system",
                    "content": "Return one JSON object only. Do not invent candidates.",
                },
                {
                    "role": "user",
                    "content": json.dumps(prompt, ensure_ascii=False),
                },
            ],
            "max_completion_tokens": 256,
            "response_format": {"type": "json_object"},
        }
        if self.base_url == MIMO_BASE_URL:
            request_body["thinking"] = {"type": "disabled"}
        else:
            # DashScope's OpenAI-compatible HTTP API uses this top-level flag.
            request_body["enable_thinking"] = False
        try:
            response = self.client.post(
                self.base_url + "/chat/completions",
                headers={"Authorization": "Bearer " + self.api_key},
                json=request_body,
            )
            response.raise_for_status()
            content = response.json()["choices"][0]["message"]["content"]
            parsed = json.loads(content)
            candidate_id = parsed["candidate_id"]
            reason = parsed["reason"]
            if (candidate_id not in candidate_ids
                    or not isinstance(reason, str)
                    or not reason.strip()):
                return None
            return ModelChoice(candidate_id=candidate_id, reason=reason.strip()[:160])
        except (httpx.HTTPError, KeyError, IndexError, TypeError, ValueError):
            return None
