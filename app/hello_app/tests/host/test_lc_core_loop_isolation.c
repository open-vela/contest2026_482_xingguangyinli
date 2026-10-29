#include "lc_checkin.h"
#include "lc_presence_flow.h"
#include "lc_product_accessory.h"
#include "lc_product_flow.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static lc_product_result_t takeout_recommendation(void)
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

static lc_accessory_event_t checkin_event(const char *device_id,
                                           const char *session_id)
{
  lc_accessory_event_t event;
  char wire[LC_ACCESSORY_WIRE_MAX];
  int length;

  length = snprintf(wire, sizeof(wire), "LC1,%s,%s,1,checkin,1",
                    device_id, session_id);
  assert(length > 0 && (size_t)length < sizeof(wire));
  assert(lc_accessory_event_parse(wire, &event) == LC_ACCESSORY_OK);
  return event;
}

static lc_accessory_runtime_result_t accepted_checkin(
  const lc_accessory_event_t *event)
{
  lc_accessory_runtime_result_t runtime_result;

  memset(&runtime_result, 0, sizeof(runtime_result));
  runtime_result.status = LC_ACCESSORY_RUNTIME_ACCEPTED;
  runtime_result.acceptance = LC_ACCESSORY_ACCEPTED;
  runtime_result.event = *event;
  return runtime_result;
}

static void test_checkins_cannot_confirm_takeout(void)
{
  lc_product_flow_t product;
  lc_product_result_t recommendation = takeout_recommendation();
  lc_checkin_tracker_t checkins;
  lc_checkin_result_t checkin_result;
  lc_accessory_event_t cat_a = checkin_event("cat_a", "boot_a");
  lc_accessory_event_t cat_b = checkin_event("cat_b", "boot_b");
  lc_accessory_runtime_result_t accessory;

  lc_product_flow_init(&product);
  assert(lc_product_flow_start(&product, LC_CHOICE_TAKEOUT));
  assert(lc_product_flow_set_recommendation(&product, &recommendation));
  assert(product.state == LC_PRODUCT_RECOMMENDATION);
  assert(product.handoff_url[0] == '\0');

  lc_checkin_tracker_init(&checkins);
  assert(lc_checkin_tracker_accept(&checkins, &cat_a, &checkin_result) ==
         LC_CHECKIN_ACCEPTED);
  accessory = accepted_checkin(&cat_a);
  assert(lc_product_accessory_decide(LC_PRESENCE_SOURCE_ACCESSORY,
                                     &accessory, &product) ==
         LC_PRODUCT_ACCESSORY_NONE);
  assert(product.state == LC_PRODUCT_RECOMMENDATION);
  assert(product.handoff_url[0] == '\0');

  assert(lc_checkin_tracker_accept(&checkins, &cat_b, &checkin_result) ==
         LC_CHECKIN_ACCEPTED);
  accessory = accepted_checkin(&cat_b);
  assert(lc_product_accessory_decide(LC_PRESENCE_SOURCE_ACCESSORY,
                                     &accessory, &product) ==
         LC_PRODUCT_ACCESSORY_NONE);
  assert(lc_checkin_tracker_checked_in(&checkins, LC_CHECKIN_CAT_A));
  assert(lc_checkin_tracker_checked_in(&checkins, LC_CHECKIN_CAT_B));
  assert(product.state == LC_PRODUCT_RECOMMENDATION);
  assert(product.handoff_url[0] == '\0');

  assert(!lc_product_flow_set_handoff(
    &product, "https://living-canvas.example/c/12ab34cd56ef"));
  assert(product.state == LC_PRODUCT_RECOMMENDATION);
}

static void test_presence_greeting_returns_home_without_touching_takeout(void)
{
  lc_product_flow_t product;
  lc_product_result_t recommendation = takeout_recommendation();
  lc_presence_tracker_t presence;
  lc_presence_observation_t enter;
  lc_presence_observation_t stay;
  lc_state_machine_t room;
  lc_actions_t actions;

  lc_product_flow_init(&product);
  assert(lc_product_flow_start(&product, LC_CHOICE_TAKEOUT));
  assert(lc_product_flow_set_recommendation(&product, &recommendation));

  lc_presence_tracker_init(&presence, 70u, 500u);
  lc_state_init(&room, 30000u);
  assert(lc_presence_from_binary(LC_PRESENCE_SOURCE_MMWAVE, true,
                                 1000u, &enter));
  assert(lc_presence_from_binary(LC_PRESENCE_SOURCE_MMWAVE, true,
                                 1200u, &stay));

  actions = lc_presence_flow_handle(&presence, &room, &enter);
  assert((actions & LC_ACTION_PLAY_GREETING) != 0u);
  assert(room.state == LC_STATE_GREETING);
  assert(lc_presence_flow_handle(&presence, &room, &stay) ==
         LC_ACTION_NONE);
  assert(lc_state_handle(&room, LC_EVENT_GREETING_FINISHED, 1800u) ==
         LC_ACTION_SHOW_STATE);
  assert(room.state == LC_STATE_IDLE);

  assert(product.state == LC_PRODUCT_RECOMMENDATION);
  assert(product.handoff_url[0] == '\0');
}

int main(void)
{
  test_checkins_cannot_confirm_takeout();
  test_presence_greeting_returns_home_without_touching_takeout();
  puts("PASS: Living Canvas core-loop isolation");
  return 0;
}
