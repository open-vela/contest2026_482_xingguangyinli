#include "lc_dinner.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static lc_dinner_candidate_t candidate(const char *name,
                                       unsigned int price_cents,
                                       unsigned int allergens,
                                       bool spicy,
                                       bool requires_cooking)
{
  lc_dinner_candidate_t item = {0};

  snprintf(item.name, sizeof(item.name), "%s", name);
  item.price_cents = price_cents;
  item.allergen_flags = allergens;
  item.spicy = spicy;
  item.requires_cooking = requires_cooking;
  return item;
}

static void test_missing_budget_requests_clarification(void)
{
  lc_dinner_constraints_t constraints = {0};
  lc_dinner_result_t result = {0};

  assert(lc_dinner_select(NULL, 0u, &constraints, &result) ==
         LC_DINNER_NEEDS_BUDGET);
  assert(result.next_state == LC_STATE_LISTENING);
  assert(result.candidate_count == 0u);
}

static void test_filters_allergens_and_budget(void)
{
  lc_dinner_candidate_t catalog[] = {
    candidate("peanut-noodles", 1800u, LC_ALLERGEN_PEANUT, false, false),
    candidate("rice-bowl", 2200u, LC_ALLERGEN_NONE, false, false),
    candidate("premium-salad", 4800u, LC_ALLERGEN_NONE, false, false),
  };
  lc_dinner_constraints_t constraints = {
    .budget_known = true,
    .max_budget_cents = 3000u,
    .excluded_allergen_flags = LC_ALLERGEN_PEANUT,
    .allow_spicy = true,
    .allow_cooking = true,
  };
  lc_dinner_result_t result = {0};

  assert(lc_dinner_select(catalog, 3u, &constraints, &result) == LC_DINNER_OK);
  assert(result.candidate_count == 1u);
  assert(strcmp(result.candidates[0].name, "rice-bowl") == 0);
  assert((result.actions & LC_ACTION_SHOW_QR) == 0u);
}

static void test_transient_no_spicy_filter(void)
{
  lc_dinner_candidate_t catalog[] = {
    candidate("spicy-soup", 2000u, LC_ALLERGEN_NONE, true, false),
    candidate("mild-soup", 2000u, LC_ALLERGEN_NONE, false, false),
  };
  lc_dinner_constraints_t constraints = {
    .budget_known = true,
    .max_budget_cents = 3000u,
    .excluded_allergen_flags = LC_ALLERGEN_NONE,
    .allow_spicy = false,
    .allow_cooking = true,
  };
  lc_dinner_result_t result = {0};

  assert(lc_dinner_select(catalog, 2u, &constraints, &result) == LC_DINNER_OK);
  assert(result.candidate_count == 1u);
  assert(strcmp(result.candidates[0].name, "mild-soup") == 0);
}

static void test_not_wanting_to_cook_rejects_complex_meal(void)
{
  lc_dinner_candidate_t catalog[] = {
    candidate("cook-stew", 2000u, LC_ALLERGEN_NONE, false, true),
    candidate("ready-bowl", 2000u, LC_ALLERGEN_NONE, false, false),
  };
  lc_dinner_constraints_t constraints = {
    .budget_known = true,
    .max_budget_cents = 3000u,
    .allow_spicy = true,
    .allow_cooking = false,
  };
  lc_dinner_result_t result = {0};

  assert(lc_dinner_select(catalog, 2u, &constraints, &result) == LC_DINNER_OK);
  assert(result.candidate_count == 1u);
  assert(strcmp(result.candidates[0].name, "ready-bowl") == 0);
}

static void test_result_rejects_unknown_or_privileged_actions(void)
{
  lc_dinner_result_t result = {
    .candidate_count = 1u,
    .next_state = LC_STATE_RECOMMENDATION,
    .actions = LC_ACTION_SHOW_STATE,
  };

  assert(lc_dinner_validate_result(&result));
  result.actions |= LC_ACTION_SHOW_QR;
  assert(!lc_dinner_validate_result(&result));
  result.actions = LC_ACTION_SHOW_STATE;
  result.actions |= LC_ACTION_SET_LIGHT;
  assert(!lc_dinner_validate_result(&result));
  result.actions = (lc_actions_t)(1u << 31);
  assert(!lc_dinner_validate_result(&result));
  result.candidate_count = 0u;
  result.next_state = LC_STATE_LISTENING;
  result.actions = LC_ACTION_SHOW_STATE | LC_ACTION_SHOW_QR;
  assert(!lc_dinner_validate_result(&result));
}

int main(void)
{
  test_missing_budget_requests_clarification();
  test_filters_allergens_and_budget();
  test_transient_no_spicy_filter();
  test_not_wanting_to_cook_rejects_complex_meal();
  test_result_rejects_unknown_or_privileged_actions();
  puts("PASS: lc_dinner");
  return 0;
}
