#include "lc_dinner.h"

#include <stdio.h>
#include <string.h>

static bool has_terminator(const char *text, size_t capacity)
{
  return memchr(text, '\0', capacity) != NULL;
}

bool lc_dinner_validate_result(const lc_dinner_result_t *result)
{
  const lc_actions_t allowed_actions = LC_ACTION_SHOW_STATE;
  size_t index;

  if (result == NULL || result->candidate_count > LC_DINNER_MAX_CANDIDATES)
    {
      return false;
    }

  if ((result->actions & ~allowed_actions) != 0u)
    {
      return false;
    }

  if (result->candidate_count > 0u &&
      result->next_state != LC_STATE_RECOMMENDATION)
    {
      return false;
    }

  if (result->candidate_count == 0u &&
      result->next_state != LC_STATE_LISTENING &&
      result->next_state != LC_STATE_ERROR)
    {
      return false;
    }

  if (!has_terminator(result->reply, sizeof(result->reply)))
    {
      return false;
    }

  for (index = 0u; index < result->candidate_count; index++)
    {
      const lc_dinner_candidate_t *candidate = &result->candidates[index];
      if (!has_terminator(candidate->name, sizeof(candidate->name)) ||
          !has_terminator(candidate->reason, sizeof(candidate->reason)) ||
          !has_terminator(candidate->qr_target,
                          sizeof(candidate->qr_target)))
        {
          return false;
        }
    }

  return true;
}

lc_dinner_status_t lc_dinner_select(const lc_dinner_candidate_t *catalog,
                                    size_t catalog_count,
                                    const lc_dinner_constraints_t *constraints,
                                    lc_dinner_result_t *result)
{
  size_t index;

  if (constraints == NULL || result == NULL ||
      (catalog == NULL && catalog_count != 0u))
    {
      return LC_DINNER_INVALID;
    }

  memset(result, 0, sizeof(*result));
  result->next_state = LC_STATE_LISTENING;
  result->actions = LC_ACTION_SHOW_STATE;

  if (!constraints->budget_known)
    {
      snprintf(result->reply, sizeof(result->reply),
               "What is your budget for dinner?");
      return LC_DINNER_NEEDS_BUDGET;
    }

  for (index = 0u;
       index < catalog_count &&
       result->candidate_count < LC_DINNER_MAX_CANDIDATES;
       index++)
    {
      const lc_dinner_candidate_t *candidate = &catalog[index];

      if (candidate->price_cents > constraints->max_budget_cents ||
          (candidate->allergen_flags &
           constraints->excluded_allergen_flags) != 0u ||
          (!constraints->allow_spicy && candidate->spicy) ||
          (!constraints->allow_cooking && candidate->requires_cooking))
        {
          continue;
        }

      result->candidates[result->candidate_count] = *candidate;
      result->candidate_count++;
    }

  if (result->candidate_count == 0u)
    {
      result->next_state = LC_STATE_ERROR;
      snprintf(result->reply, sizeof(result->reply),
               "No safe dinner candidate matches the confirmed constraints.");
      return LC_DINNER_NO_MATCH;
    }

  result->next_state = LC_STATE_RECOMMENDATION;
  result->actions = LC_ACTION_SHOW_STATE;
  snprintf(result->reply, sizeof(result->reply),
           "Here are %u dinner options that match.",
           (unsigned int)result->candidate_count);
  return LC_DINNER_OK;
}
