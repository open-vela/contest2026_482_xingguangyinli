import math
import re
import unicodedata
from typing import List, Optional, Tuple

from .schema import (
    BoardHardConstraints,
    HardConstraints,
    MealChoice,
    RoleContribution,
    SoftPreferences,
)


_CN_DIGIT = r"[一二两三四五六七八九]"
_CN_NUMBER = (rf"(?:{_CN_DIGIT}百(?:零{_CN_DIGIT}|{_CN_DIGIT}十{_CN_DIGIT}?)?"
              rf"|{_CN_DIGIT}?十{_CN_DIGIT}?|[零〇一二两三四五六七八九])")
_DIGITS = dict(zip("零〇一二两三四五六七八九", (0, 0, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9)))
_NUMBER = (rf"(?:[+\-负]?(?:半|[0-9]+(?:\.[0-9]+)?"
           rf"|{_CN_NUMBER}(?:点[零〇一二两三四五六七八九]+)?))")


class SpokenConstraintError(ValueError):
    """An explicit numeric limit cannot safely become a default."""


def _number_value(token: str) -> float:
    if token[0] in "+-负":
        return (-1 if token[0] in "-负" else 1) * _number_value(token[1:])
    if token[0] in "0123456789":
        return float(token)
    if token == "半":
        return 0.5
    if "点" in token:
        whole, fraction = token.split("点")
        return _number_value(whole) + float("0." + "".join(str(_DIGITS[c]) for c in fraction))
    total = current = 0
    for character in token:
        if character in ("十", "百"):
            total += (current or 1) * (10 if character == "十" else 100)
            current = 0
        else:
            current = _DIGITS[character]
    return float(total + current)


def _spoken_quantity(text: str, units: str):
    # Consume a complete range so "25元到40元" means the upper limit, not25.
    # Do not reinterpret a trailing fragment of a decimal/Chinese number.
    text = unicodedata.normalize("NFKC", text)
    matches = list(re.finditer(
        rf"(?<![0-9.零〇一二两三四五六七八九十百千万点负半个+\-])"
        rf"(?:{_NUMBER}\s*(?:{units})?\s*(?:到|至|[-~～—])\s*)?"
        rf"(?P<limit>{_NUMBER})\s*(?P<unit>{units})", text,
    ))
    if not matches:
        return None
    match = matches[0]
    for later in matches[1:]:
        between = text[match.end():later.start()]
        if re.search(r"(?:改成|改为|最多|不超过|上限|预算)\s*$", between) or re.search(
            r"(?:不对|太贵|太久)[,，、\s]*$", between,
        ):
            match = later
        elif units == r"元|块" and re.search(r"(?:配送费|运费)\s*$", between):
            continue
        else:
            # Multiple unqualified quantities need clarification, not a guess.
            raise SpokenConstraintError("invalid_spoken_constraints")
    if units == r"元|块" and re.search(
        rf"(?:预算|改成|改为|最多|不超过|上限)\s*{_NUMBER}"
        r"(?![0-9.零〇一二两三四五六七八九十百千万点])"
        r"(?!\s*(?:分钟|(?:个)?小时|刻钟|个人|人))", text[match.end():],
    ):
        raise SpokenConstraintError("invalid_spoken_constraints")
    value = _number_value(match["limit"])
    if not math.isfinite(value) or value <= 0:
        raise SpokenConstraintError("invalid_spoken_constraints")
    return value, match["unit"]

_CUISINES = ("中式", "川菜", "西式", "本地特色", "粤菜", "湘菜", "鲁菜", "苏菜", "浙菜", "闽菜", "徽菜", "日式", "韩式")
_FOOD_TAGS = ("米饭", "面食", "暖胃", "轻食", "高蛋白", "素食", "酸甜")
_FOOD_TERM = "(?:" + "|".join(re.escape(value) for value in _CUISINES + _FOOD_TAGS) + ")"
_LIST_JOIN = r"\s*(?:和|、|或|以及|跟|及)\s*"


def _rejected(text: str, token: str) -> bool:
    negative = r"(?:不想吃|不爱吃|不喜欢(?:吃)?|不能吃|不吃|不要(?:吃)?|别(?:吃)?)"
    escaped = re.escape(token)
    before = negative + r"\s*(?:" + _FOOD_TERM + _LIST_JOIN + r")*" + escaped
    after = escaped + r"(?:" + _LIST_JOIN + _FOOD_TERM + r")*\s*(?:都)?" + negative
    return bool(re.search(before, text) or re.search(after, text))


def _budget_from_text(text: str, fallback: float = 50.0) -> float:
    quantity = _spoken_quantity(text, r"元|块")
    if quantity is None and re.search(
        r"预算[^,，。]{0,10}[0-9零〇一二两三四五六七八九十百千万点负]",
        unicodedata.normalize("NFKC", text),
    ):
        raise SpokenConstraintError("invalid_spoken_constraints")
    return quantity[0] if quantity is not None else fallback


def _delivery_from_text(text: str, fallback: int = 30) -> int:
    quantity = _spoken_quantity(text, r"分钟|(?:个)?小时|刻钟")
    if quantity is not None:
        amount, unit = quantity
        factor = 60 if unit.endswith("小时") else 15 if unit == "刻钟" else 1
        minutes = math.floor(amount * factor)
        if minutes < 1:
            raise SpokenConstraintError("invalid_spoken_constraints")
        return minutes
    if re.search(r"[0-9零〇一二两三四五六七八九十百千万点负].{0,5}(?:分钟|小时|刻钟)",
                 unicodedata.normalize("NFKC", text)):
        raise SpokenConstraintError("invalid_spoken_constraints")
    return fallback


def _preferred_tags(text: str, choice: MealChoice) -> List[str]:
    tags = []
    for token in _FOOD_TAGS:
        if token in text and not _rejected(text, token):
            tags.append(token)
    if choice == MealChoice.MYSTERY:
        tags.extend(["探索", "盲盒"])
    if choice == MealChoice.HOME:
        tags.append("家常")
    return list(dict.fromkeys(tags))


def build_dialogue_inputs(
    choice: MealChoice, transcript: str,
    previous_constraints: Optional[HardConstraints] = None,
    previous_preferences: Optional[SoftPreferences] = None,
) -> Tuple[BoardHardConstraints, SoftPreferences, List[RoleContribution]]:
    channel = "home" if choice == MealChoice.HOME else "delivery"
    novelty = {
        MealChoice.TAKEOUT: "balanced",
        MealChoice.MYSTERY: "exploratory",
        MealChoice.HOME: "familiar",
    }[choice]
    tags = _preferred_tags(transcript, choice)
    allergens = []
    for allergen in ("花生", "乳制品", "鱼类", "麸质"):
        if allergen + "过敏" in transcript or "不要" + allergen in transcript:
            allergens.append(allergen)

    previous = previous_constraints or HardConstraints()
    rejected = [token for token in _CUISINES + _FOOD_TAGS if _rejected(transcript, token)]

    constraints = BoardHardConstraints(
        max_total_price_cny=_budget_from_text(transcript, previous.max_total_price_cny or 50.0),
        max_delivery_minutes=_delivery_from_text(transcript, previous.max_delivery_minutes or 30),
        channel=channel,
        allergens=list(dict.fromkeys(previous.allergens + allergens)),
        diet_taboos=previous.diet_taboos,
        dislikes=list(dict.fromkeys(previous.dislikes + rejected)),
    )
    preferences = SoftPreferences(
        cuisines=[token for token in _CUISINES if token in transcript and not _rejected(transcript, token)]
        or (["中式"] if "家常" in transcript and not _rejected(transcript, "家常") else []),
        temperatures=["热"] if "热" in transcript or "暖" in transcript else [],
        preferred_tags=tags,
        novelty=novelty,
    )
    if previous_preferences is not None:
        # Unmentioned current-session fields survive a short follow-up. New
        # positive requirements replace that field; new exclusions remove old
        # positive preferences before remembered defaults are considered.
        changes = {}
        for field in ("cuisines", "temperatures", "preferred_tags"):
            current = getattr(preferences, field)
            carried = getattr(previous_preferences, field)
            changes[field] = [value for value in (current or carried)
                              if value not in constraints.dislikes]
        preferences = preferences.model_copy(update=changes)
    contributions = [
        RoleContribution(
            role_id="beagle",
            summary=f"协调{choice.value}流程，并保留用户最终确认",
        ),
        RoleContribution(
            role_id="cat",
            summary=(
                "提取口味线索：" + "、".join(tags)
                if tags
                else "本轮没有强加未确认的口味偏好"
            ),
        ),
        RoleContribution(
            role_id="pigeon",
            summary="本轮没有可靠实时天气，因此不把天气当作事实参与决策",
        ),
        RoleContribution(
            role_id="pig",
            summary=(
                "检查居家烹饪的简单性与常见食材"
                if choice == MealChoice.HOME
                else "复核预算、时效与已声明过敏项"
            ),
        ),
        RoleContribution(
            role_id="alien",
            summary={
                MealChoice.TAKEOUT: "把新奇度控制在均衡范围",
                MealChoice.MYSTERY: "提高探索度，但不越过硬约束",
                MealChoice.HOME: "优先熟悉、可执行的家常方案",
            }[choice],
        ),
    ]
    return constraints, preferences, contributions
