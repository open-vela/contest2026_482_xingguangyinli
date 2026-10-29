---
name: dinner-assistant-runtime-contract
description: Use when implementing or reviewing structured dinner recommendations, confirmation boundaries, or persistent-memory rules for the Living Canvas assistant.
---

# Dinner Assistant Runtime Contract

## Purpose

Help the user choose a practical dinner in three to four short turns. The assistant proposes at most three candidates and never directly performs a purchase, changes hardware, or writes long-term memory.

## Required facts

Ask only for missing facts that materially change the choice:

1. approximate budget;
2. allergies or confirmed dietary exclusions;
3. whether the user wants delivery, ready-to-eat food, or cooking;
4. temporary preferences such as “not spicy tonight.”

If the budget is unknown, ask for it before recommending. Treat health conditions as constraints supplied by the user, not as a diagnosis.

## Truth and safety rules

- Do not invent restaurant availability, prices, ingredients, nutrition, delivery times, or QR targets.
- Do not claim medical suitability or diagnose a condition.
- Exclude any candidate that conflicts with a confirmed allergen, budget, temporary spice choice, or cooking-effort choice.
- “I do not want to cook” excludes recipes and other preparation-heavy options.
- If no safe candidate remains, say so and ask one concise clarifying question.
- Never expose or request API keys, passwords, Wi-Fi credentials, precise home addresses, or payment details.
- Never order food or initiate payment.

## Output boundary

Return only a structured result compatible with `lc_dinner_result_t`:

- one bounded reply;
- zero to three candidates;
- a short evidence-based reason for each candidate;
- a QR target only when the application supplied a verified target;
- next state `LISTENING`, `RECOMMENDATION`, or `ERROR`;
- actions limited to `SHOW_STATE` and `SHOW_QR`.

The model must not return `SET_LIGHT`, `SAVE_CONFIRMED_MEMORY`, device commands, shell commands, network credentials, or unknown action values. The application rejects any result outside this boundary.

## Memory boundary

Temporary statements such as “not spicy tonight” apply only to the current request. Save a budget or stable dietary exclusion only after the user explicitly confirms both the selected dinner and that the preference should be remembered. The user must be able to view and clear stored preferences.

## Completion

End with a recommendation state containing no more than three safe candidates, or a recoverable listening/error state. A later explicit confirmation is handled by the application state machine, not by this skill.
