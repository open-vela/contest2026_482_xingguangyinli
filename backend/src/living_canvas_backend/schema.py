from enum import Enum
from typing import Dict, List, Literal, Optional

from pydantic import BaseModel, ConfigDict, Field, StrictBool, StrictInt, field_validator


class StrictModel(BaseModel):
    model_config = ConfigDict(extra="forbid")


class HardConstraints(StrictModel):
    allergens: List[str] = Field(default_factory=list)
    diet_taboos: List[str] = Field(default_factory=list)
    dislikes: List[str] = Field(default_factory=list)
    max_total_price_cny: Optional[float] = Field(default=None, gt=0)
    max_delivery_minutes: Optional[int] = Field(default=None, gt=0)
    channel: Optional[str] = None


class SoftPreferences(StrictModel):
    cuisines: List[str] = Field(default_factory=list)
    temperatures: List[str] = Field(default_factory=list)
    preferred_tags: List[str] = Field(default_factory=list)
    novelty: str = Field(default="balanced", pattern="^(familiar|balanced|exploratory)$")


class MealContext(StrictModel):
    people: int = Field(default=1, ge=1, le=20)
    occasion: str = "日常用餐"
    location_hint: Optional[str] = None


class Candidate(StrictModel):
    candidate_id: str
    name: str
    cuisine: str
    temperature: str
    price_cny: float = Field(gt=0)
    delivery_minutes: int = Field(gt=0)
    channels: List[str]
    ingredients: List[str] = Field(default_factory=list)
    allergens: List[str] = Field(default_factory=list)
    tags: List[str] = Field(default_factory=list)
    novelty_score: float = Field(default=0.5, ge=0, le=1)
    search_query: Optional[str] = None


class Recommendation(StrictModel):
    candidate: Candidate
    reason: str
    source: str
    rules_fallback: bool = False
    score: float = 0


class SessionState(str, Enum):
    CREATED = "created"
    CANDIDATE = "candidate"
    CONFIRMING = "confirming"
    HANDED_OFF = "handed_off"
    CANCELLED = "cancelled"


class MealChoice(str, Enum):
    TAKEOUT = "takeout"
    MYSTERY = "mystery"
    HOME = "home"


class DecisionSession(StrictModel):
    session_id: str
    state: SessionState = SessionState.CREATED
    context: MealContext = Field(default_factory=MealContext)
    hard_constraints: HardConstraints = Field(default_factory=HardConstraints)
    soft_preferences: SoftPreferences = Field(default_factory=SoftPreferences)
    recommendation: Optional[Recommendation] = None
    rejected_candidate_ids: List[str] = Field(default_factory=list)
    metadata: Dict[str, str] = Field(default_factory=dict)


class SessionCreateRequest(StrictModel):
    device_id: Optional[str] = Field(default=None, max_length=80)


class BoardHardConstraints(StrictModel):
    max_total_price_cny: float = Field(gt=0)
    max_delivery_minutes: int = Field(gt=0)
    channel: str = Field(min_length=1)
    allergens: List[str] = Field(default_factory=list)
    diet_taboos: List[str] = Field(default_factory=list)
    dislikes: List[str] = Field(default_factory=list)

    def as_domain(self) -> HardConstraints:
        return HardConstraints(**self.model_dump())


class BoardInputRequest(StrictModel):
    session_id: str
    context: MealContext
    hard_constraints: BoardHardConstraints
    soft_preferences: SoftPreferences = Field(default_factory=SoftPreferences)


class DialogueRequest(StrictModel):
    session_id: str
    choice: MealChoice
    transcript: str = Field(min_length=1, max_length=500)
    reject_current: bool = False

    @field_validator("transcript")
    @classmethod
    def transcript_must_contain_speech(cls, value: str) -> str:
        value = value.strip()
        if not value:
            raise ValueError("transcript_empty")
        return value


class RoleContribution(StrictModel):
    role_id: str = Field(pattern="^(beagle|cat|pigeon|pig|alien)$")
    summary: str = Field(min_length=1, max_length=160)


class DeviceEventRequest(StrictModel):
    session_id: str
    event: str


class AccessoryEventRequest(StrictModel):
    device_id: Literal["cat_a", "cat_b", "mmwave_bridge"]
    session_id: str = Field(
        min_length=1,
        max_length=23,
        pattern=r"^[A-Za-z0-9._-]+$",
    )
    sequence: StrictInt = Field(ge=1, le=2_147_483_647)
    event: Literal["checkin", "presence"]
    value: StrictBool


class ConfirmRequest(StrictModel):
    session_id: str
    platform: str


class FeedbackRequest(StrictModel):
    session_id: str
    liked: bool
    note: Optional[str] = Field(default=None, max_length=500)
    temporary_preferences: List[str] = Field(default_factory=list)
    remember_preferences: List[str] = Field(default_factory=list)
