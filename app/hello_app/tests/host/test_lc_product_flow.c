#include "lc_product_flow.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static lc_product_result_t valid_takeout_result(void)
{
  lc_product_result_t result =
  {
    .choice = LC_CHOICE_TAKEOUT,
    .source = LC_PRODUCT_SOURCE_RULES,
    .requires_handoff = true,
    .rules_fallback = true,
    .name = "Tomato beef rice",
    .reason = "Warm, balanced and within budget",
    .price = "About CNY 32",
    .spoken_reply = "I found a warm tomato beef rice option for you."
  };

  return result;
}

static void test_real_product_happy_path(void)
{
  lc_product_flow_t flow;
  lc_product_result_t result = valid_takeout_result();

  lc_product_flow_init(&flow);
  assert(flow.state == LC_PRODUCT_CHOICE);
  assert(flow.error == LC_PRODUCT_ERROR_NONE);

  assert(lc_product_flow_start(&flow, LC_CHOICE_TAKEOUT));
  assert(flow.state == LC_PRODUCT_REQUESTING);
  assert(flow.choice == LC_CHOICE_TAKEOUT);

  assert(lc_product_flow_set_recommendation(&flow, &result));
  assert(flow.state == LC_PRODUCT_RECOMMENDATION);
  assert(strcmp(flow.result.name, "Tomato beef rice") == 0);
  assert(flow.result.rules_fallback);

  assert(lc_product_flow_begin_confirmation(&flow));
  assert(flow.state == LC_PRODUCT_CONFIRMING);
  assert(lc_product_flow_set_handoff(
    &flow, "https://living-canvas.example/c/12ab34cd56ef"));
  assert(flow.state == LC_PRODUCT_QR);
  assert(strcmp(flow.handoff_url,
                "https://living-canvas.example/c/12ab34cd56ef") == 0);
}

static void test_requires_explicit_confirmation_before_qr(void)
{
  lc_product_flow_t flow;
  lc_product_result_t result = valid_takeout_result();

  lc_product_flow_init(&flow);
  assert(lc_product_flow_start(&flow, LC_CHOICE_TAKEOUT));
  assert(lc_product_flow_set_recommendation(&flow, &result));
  assert(!lc_product_flow_set_handoff(
    &flow, "https://living-canvas.example/c/12ab34cd56ef"));
  assert(flow.state == LC_PRODUCT_RECOMMENDATION);
}

static void test_rejects_invalid_or_stale_results(void)
{
  lc_product_flow_t flow;
  lc_product_result_t result = valid_takeout_result();

  lc_product_flow_init(&flow);
  assert(!lc_product_flow_set_recommendation(&flow, &result));
  assert(lc_product_flow_start(&flow, LC_CHOICE_MYSTERY));
  assert(!lc_product_flow_set_recommendation(&flow, &result));
  assert(flow.state == LC_PRODUCT_REQUESTING);

  result.choice = LC_CHOICE_MYSTERY;
  result.name[0] = '\0';
  assert(!lc_product_flow_set_recommendation(&flow, &result));
  assert(flow.state == LC_PRODUCT_REQUESTING);

  result = valid_takeout_result();
  result.choice = LC_CHOICE_MYSTERY;
  assert(lc_product_flow_set_recommendation(&flow, &result));
  assert(lc_product_flow_begin_confirmation(&flow));
  assert(!lc_product_flow_set_handoff(&flow, "javascript:alert(1)"));
  assert(flow.state == LC_PRODUCT_CONFIRMING);
}

static void test_rejects_control_characters_from_external_results(void)
{
  lc_product_flow_t flow;
  lc_product_result_t result = valid_takeout_result();

  lc_product_flow_init(&flow);
  assert(lc_product_flow_start(&flow, LC_CHOICE_TAKEOUT));
  memcpy(result.spoken_reply, "safe\nunsafe", sizeof("safe\nunsafe"));
  assert(!lc_product_flow_set_recommendation(&flow, &result));

  result = valid_takeout_result();
  assert(lc_product_flow_set_recommendation(&flow, &result));
  assert(lc_product_flow_begin_confirmation(&flow));
  assert(!lc_product_flow_set_handoff(
    &flow, "https://living-canvas.example/c/good\nbad"));
  assert(flow.state == LC_PRODUCT_CONFIRMING);
}

static void test_rejects_nonterminated_handoff_url(void)
{
  lc_product_flow_t flow;
  lc_product_result_t result = valid_takeout_result();
  char url[LC_PRODUCT_URL_MAX];

  memset(url, 'a', sizeof(url));
  memcpy(url, "https://", sizeof("https://") - 1u);
  lc_product_flow_init(&flow);
  assert(lc_product_flow_start(&flow, LC_CHOICE_TAKEOUT));
  assert(lc_product_flow_set_recommendation(&flow, &result));
  assert(lc_product_flow_begin_confirmation(&flow));
  assert(!lc_product_flow_set_handoff(&flow, url));
  assert(flow.state == LC_PRODUCT_CONFIRMING);
}

static void test_rejects_non_eleme_or_non_compact_handoff_urls(void)
{
  lc_product_flow_t flow;
  lc_product_result_t result = valid_takeout_result();

  lc_product_flow_init(&flow);
  assert(lc_product_flow_start(&flow, LC_CHOICE_TAKEOUT));
  assert(lc_product_flow_set_recommendation(&flow, &result));
  assert(lc_product_flow_begin_confirmation(&flow));
  assert(!lc_product_flow_set_handoff(
    &flow, "http://living-canvas.example/c/12ab34cd56ef"));
  assert(!lc_product_flow_set_handoff(
    &flow, "https://living-canvas.example/console"));
  assert(!lc_product_flow_set_handoff(
    &flow, "https://living-canvas.example/c/12ab34cd56ef?platform=meituan"));
  assert(!lc_product_flow_set_handoff(
    &flow, "https://living-canvas.example/c/12ab34cd56eg"));
  assert(flow.state == LC_PRODUCT_CONFIRMING);
}

static void test_home_meal_completes_without_fake_order(void)
{
  lc_product_flow_t flow;
  lc_product_result_t result =
  {
    .choice = LC_CHOICE_HOME,
    .source = LC_PRODUCT_SOURCE_RULES,
    .requires_handoff = false,
    .rules_fallback = true,
    .name = "Tomato egg rice",
    .reason = "Uses common ingredients and takes about 15 minutes",
    .price = "Home ingredients",
    .spoken_reply = "Let's make tomato egg rice at home."
  };

  lc_product_flow_init(&flow);
  assert(lc_product_flow_start(&flow, LC_CHOICE_HOME));
  assert(lc_product_flow_set_recommendation(&flow, &result));
  assert(lc_product_flow_begin_confirmation(&flow));
  assert(lc_product_flow_complete_local(&flow));
  assert(flow.state == LC_PRODUCT_COMPLETE);
  assert(flow.handoff_url[0] == '\0');
}

static void test_error_is_visible_and_retryable(void)
{
  lc_product_flow_t flow;

  lc_product_flow_init(&flow);
  assert(lc_product_flow_start(&flow, LC_CHOICE_TAKEOUT));
  lc_product_flow_fail(&flow, LC_PRODUCT_ERROR_NETWORK,
                       "backend timeout", true);
  assert(flow.state == LC_PRODUCT_ERROR);
  assert(flow.error == LC_PRODUCT_ERROR_NETWORK);
  assert(flow.retryable);
  assert(strcmp(flow.error_message, "backend timeout") == 0);

  assert(lc_product_flow_retry(&flow));
  assert(flow.state == LC_PRODUCT_CHOICE);
  assert(flow.error == LC_PRODUCT_ERROR_NONE);

  lc_product_flow_fail(&flow, LC_PRODUCT_ERROR_SCHEMA,
                       "invalid response", false);
  assert(!lc_product_flow_retry(&flow));
  lc_product_flow_reset(&flow);
  assert(flow.state == LC_PRODUCT_CHOICE);
}

int main(void)
{
  test_real_product_happy_path();
  test_requires_explicit_confirmation_before_qr();
  test_rejects_invalid_or_stale_results();
  test_rejects_control_characters_from_external_results();
  test_rejects_nonterminated_handoff_url();
  test_rejects_non_eleme_or_non_compact_handoff_urls();
  test_home_meal_completes_without_fake_order();
  test_error_is_visible_and_retryable();
  puts("PASS: Living Canvas real product flow");
  return 0;
}
