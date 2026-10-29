#include "lc_product_dialogue.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_choice_has_no_agent_bubbles(void)
{
  lc_product_flow_t flow;
  lc_product_dialogue_t dialogue;

  lc_product_flow_init(&flow);
  assert(lc_product_dialogue_build(&flow, &dialogue));
  assert(!dialogue.visible);
}

static void test_requesting_shows_two_distinct_agents(void)
{
  lc_product_flow_t flow;
  lc_product_dialogue_t dialogue;

  lc_product_flow_init(&flow);
  assert(lc_product_flow_start(&flow, LC_CHOICE_TAKEOUT));
  assert(lc_product_dialogue_build(&flow, &dialogue));
  assert(dialogue.visible);
  assert(strstr(dialogue.coordinator, "比格犬") != NULL);
  assert(strstr(dialogue.companion, "猫咪") != NULL);
  assert(strcmp(dialogue.coordinator, dialogue.companion) != 0);
}

static void test_bubbles_are_revealed_like_a_short_chat(void)
{
  lc_product_flow_t flow;
  lc_product_dialogue_t dialogue;
  uint8_t mask;

  lc_product_flow_init(&flow);
  assert(lc_product_dialogue_build(&flow, &dialogue));
  assert(lc_product_dialogue_reveal_mask(&dialogue, 0u) == 0u);

  assert(lc_product_flow_start(&flow, LC_CHOICE_TAKEOUT));
  assert(lc_product_dialogue_build(&flow, &dialogue));

  mask = lc_product_dialogue_reveal_mask(&dialogue, 0u);
  assert((mask & LC_PRODUCT_DIALOGUE_COORDINATOR_VISIBLE) != 0u);
  assert((mask & LC_PRODUCT_DIALOGUE_COMPANION_VISIBLE) == 0u);

  mask = lc_product_dialogue_reveal_mask(
    &dialogue, LC_PRODUCT_DIALOGUE_COMPANION_DELAY_MS - 1u);
  assert((mask & LC_PRODUCT_DIALOGUE_COMPANION_VISIBLE) == 0u);

  mask = lc_product_dialogue_reveal_mask(
    &dialogue, LC_PRODUCT_DIALOGUE_COMPANION_DELAY_MS);
  assert((mask & LC_PRODUCT_DIALOGUE_COORDINATOR_VISIBLE) != 0u);
  assert((mask & LC_PRODUCT_DIALOGUE_COMPANION_VISIBLE) != 0u);
}

static void test_recommendation_carries_result_and_safe_payment_boundary(void)
{
  lc_product_flow_t flow;
  lc_product_result_t result = {0};
  lc_product_dialogue_t dialogue;

  lc_product_flow_init(&flow);
  assert(lc_product_flow_start(&flow, LC_CHOICE_TAKEOUT));
  result.choice = LC_CHOICE_TAKEOUT;
  result.source = LC_PRODUCT_SOURCE_RULES;
  result.requires_handoff = true;
  strcpy(result.name, "Warm rice bowl");
  strcpy(result.reason, "Fits taste and budget");
  strcpy(result.price, "About CNY 32");
  strcpy(result.spoken_reply, "Try a warm rice bowl.");
  assert(lc_product_flow_set_recommendation(&flow, &result));
  assert(lc_product_dialogue_build(&flow, &dialogue));
  assert(strstr(dialogue.coordinator, "确认") != NULL);
  assert(strstr(dialogue.companion, "Warm rice bowl") != NULL);
  assert(strstr(dialogue.companion, "Fits taste and budget") == NULL);

  assert(lc_product_flow_begin_confirmation(&flow));
  assert(lc_product_dialogue_build(&flow, &dialogue));
  assert(strstr(dialogue.companion, "手机") != NULL);
  assert(strstr(dialogue.companion, "付款") != NULL);
}

static void test_long_reason_does_not_hide_short_head_bubbles(void)
{
  lc_product_flow_t flow;
  lc_product_dialogue_t dialogue;

  lc_product_flow_init(&flow);
  flow.state = LC_PRODUCT_RECOMMENDATION;
  flow.result.choice = LC_CHOICE_TAKEOUT;
  memset(flow.result.name, 'A', sizeof(flow.result.name) - 1u);
  flow.result.name[sizeof(flow.result.name) - 1u] = '\0';
  memset(flow.result.reason, 'B', sizeof(flow.result.reason) - 1u);
  flow.result.reason[sizeof(flow.result.reason) - 1u] = '\0';
  strcpy(flow.result.price, "About CNY 32");

  assert(lc_product_dialogue_build(&flow, &dialogue));
  assert(dialogue.visible);
  assert(strstr(dialogue.companion, flow.result.name) != NULL);
  assert(strstr(dialogue.companion, flow.result.reason) == NULL);
}

int main(void)
{
  test_choice_has_no_agent_bubbles();
  test_requesting_shows_two_distinct_agents();
  test_bubbles_are_revealed_like_a_short_chat();
  test_recommendation_carries_result_and_safe_payment_boundary();
  test_long_reason_does_not_hide_short_head_bubbles();
  lc_product_flow_t exhausted;
  lc_product_dialogue_t dialogue;
  lc_product_flow_init(&exhausted);
  lc_product_flow_fail(&exhausted, LC_PRODUCT_ERROR_NO_CANDIDATE,
                       "No suitable candidate", true);
  assert(lc_product_dialogue_build(&exhausted, &dialogue));
  assert(strstr(dialogue.coordinator, "没有合适") != NULL);
  assert(strstr(dialogue.companion, "调整条件") != NULL);
  puts("PASS: lc_product_dialogue");
  return 0;
}
