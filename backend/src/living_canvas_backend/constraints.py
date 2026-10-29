from typing import Dict, Iterable, List, Tuple

from .schema import Candidate, HardConstraints


def _normalized(values: Iterable[str]) -> Dict[str, str]:
    return {
        value.strip().casefold(): value.strip()
        for value in values
        if value and value.strip()
    }


def _first_match(needles: Iterable[str], haystack: Iterable[str]):
    wanted = _normalized(needles)
    available = _normalized(haystack)
    for normalized, original in wanted.items():
        if normalized in available:
            return original
    return None


def check_candidate(
    candidate: Candidate, constraints: HardConstraints
) -> Tuple[bool, List[str]]:
    reasons: List[str] = []
    allergen = _first_match(constraints.allergens, candidate.allergens)
    if allergen:
        reasons.append("allergen:" + allergen)

    searchable = candidate.ingredients + candidate.tags
    taboo = _first_match(constraints.diet_taboos, searchable)
    if taboo:
        reasons.append("diet_taboo:" + taboo)
    dislike = _first_match(constraints.dislikes, searchable + [candidate.cuisine, candidate.temperature])
    if dislike:
        reasons.append("dislike:" + dislike)

    if (
        constraints.max_total_price_cny is not None
        and candidate.price_cny > constraints.max_total_price_cny
    ):
        reasons.append("over_budget")
    if (
        constraints.max_delivery_minutes is not None
        and candidate.delivery_minutes > constraints.max_delivery_minutes
    ):
        reasons.append("too_slow")
    if constraints.channel and constraints.channel not in candidate.channels:
        reasons.append("wrong_channel")
    return not reasons, reasons


def filter_candidates(candidates, constraints):
    allowed = []
    rejected = {}
    for candidate in candidates:
        is_allowed, reasons = check_candidate(candidate, constraints)
        if is_allowed:
            allowed.append(candidate)
        else:
            rejected[candidate.candidate_id] = reasons
    return allowed, rejected
