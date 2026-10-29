#include "lc_product_accessory.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static lc_accessory_runtime_result_t accepted_event(
  lc_accessory_event_type_t type)
{
  lc_accessory_runtime_result_t result;

  memset(&result, 0, sizeof(result));
  result.status = LC_ACCESSORY_RUNTIME_ACCEPTED;
  result.acceptance = LC_ACCESSORY_ACCEPTED;
  result.event.type = type;
  result.event.value = 0;
  return result;
}

static lc_product_accessory_action_t decide(
  lc_presence_source_t source,
  const lc_accessory_runtime_result_t *result,
  const lc_product_flow_t *flow)
{
  return lc_product_accessory_decide(source, result, flow);
}

static void test_confirm_requires_recommendation(void)
{
  lc_accessory_runtime_result_t result = accepted_event(LC_ACCESSORY_CONFIRM);
  lc_product_flow_t flow;

  lc_product_flow_init(&flow);
  assert(decide(LC_PRESENCE_SOURCE_ACCESSORY, &result, &flow) ==
         LC_PRODUCT_ACCESSORY_NONE);

  flow.state = LC_PRODUCT_RECOMMENDATION;
  assert(decide(LC_PRESENCE_SOURCE_ACCESSORY, &result, &flow) ==
         LC_PRODUCT_ACCESSORY_CONFIRM);

  flow.state = LC_PRODUCT_CONFIRMING;
  assert(decide(LC_PRESENCE_SOURCE_ACCESSORY, &result, &flow) ==
         LC_PRODUCT_ACCESSORY_NONE);
  flow.state = LC_PRODUCT_QR;
  assert(decide(LC_PRESENCE_SOURCE_ACCESSORY, &result, &flow) ==
         LC_PRODUCT_ACCESSORY_NONE);
}

static void test_cancel_is_bounded_to_recoverable_states(void)
{
  lc_accessory_runtime_result_t result = accepted_event(LC_ACCESSORY_CANCEL);
  lc_product_flow_t flow;

  lc_product_flow_init(&flow);
  flow.state = LC_PRODUCT_RECOMMENDATION;
  assert(decide(LC_PRESENCE_SOURCE_ACCESSORY, &result, &flow) ==
         LC_PRODUCT_ACCESSORY_CANCEL);

  flow.state = LC_PRODUCT_ERROR;
  flow.retryable = true;
  assert(decide(LC_PRESENCE_SOURCE_ACCESSORY, &result, &flow) ==
         LC_PRODUCT_ACCESSORY_CANCEL);

  flow.retryable = false;
  assert(decide(LC_PRESENCE_SOURCE_ACCESSORY, &result, &flow) ==
         LC_PRODUCT_ACCESSORY_NONE);
  flow.state = LC_PRODUCT_CONFIRMING;
  assert(decide(LC_PRESENCE_SOURCE_ACCESSORY, &result, &flow) ==
         LC_PRODUCT_ACCESSORY_NONE);
  flow.state = LC_PRODUCT_QR;
  assert(decide(LC_PRESENCE_SOURCE_ACCESSORY, &result, &flow) ==
         LC_PRODUCT_ACCESSORY_NONE);
}

static void test_untrusted_or_unrelated_events_never_act(void)
{
  lc_accessory_runtime_result_t result = accepted_event(LC_ACCESSORY_CONFIRM);
  lc_product_flow_t flow;

  lc_product_flow_init(&flow);
  flow.state = LC_PRODUCT_RECOMMENDATION;

  result.status = LC_ACCESSORY_RUNTIME_REJECTED;
  result.acceptance = LC_ACCESSORY_REPLAY;
  assert(decide(LC_PRESENCE_SOURCE_ACCESSORY, &result, &flow) ==
         LC_PRODUCT_ACCESSORY_NONE);

  result = accepted_event(LC_ACCESSORY_PRESENCE);
  assert(decide(LC_PRESENCE_SOURCE_ACCESSORY, &result, &flow) ==
         LC_PRODUCT_ACCESSORY_NONE);

  result = accepted_event(LC_ACCESSORY_CONFIRM);
  result.event.value = 1;
  assert(decide(LC_PRESENCE_SOURCE_ACCESSORY, &result, &flow) ==
         LC_PRODUCT_ACCESSORY_NONE);

  result.event.value = 0;
  assert(decide(LC_PRESENCE_SOURCE_MMWAVE, &result, &flow) ==
         LC_PRODUCT_ACCESSORY_NONE);
  assert(decide(LC_PRESENCE_SOURCE_CAMERA, &result, &flow) ==
         LC_PRODUCT_ACCESSORY_NONE);

  assert(decide(LC_PRESENCE_SOURCE_ACCESSORY, NULL, &flow) ==
         LC_PRODUCT_ACCESSORY_NONE);
  assert(decide(LC_PRESENCE_SOURCE_ACCESSORY, &result, NULL) ==
         LC_PRODUCT_ACCESSORY_NONE);
}

int main(void)
{
  test_confirm_requires_recommendation();
  test_cancel_is_bounded_to_recoverable_states();
  test_untrusted_or_unrelated_events_never_act();
  puts("PASS: lc_product_accessory");
  return 0;
}
