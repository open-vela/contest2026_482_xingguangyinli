#include "lc_accessory_runtime.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static lc_accessory_runtime_status_t feed_line(
  lc_accessory_runtime_t *runtime,
  const char *line,
  uint64_t now_ms,
  lc_accessory_runtime_result_t *result)
{
  size_t index;

  for (index = 0u; line[index] != '\0'; index++)
    {
      assert(lc_accessory_runtime_feed(runtime, (uint8_t)line[index], now_ms,
                                       result) ==
             LC_ACCESSORY_RUNTIME_NONE);
    }

  return lc_accessory_runtime_feed(runtime, '\n', now_ms, result);
}

static void test_continuous_camera_presence_and_replay(void)
{
  lc_state_machine_t state;
  lc_accessory_runtime_t runtime;
  lc_accessory_runtime_result_t result;

  lc_state_init(&state, 30000u);
  assert(lc_accessory_runtime_init(&runtime, &state,
                                   LC_PRESENCE_SOURCE_CAMERA, 70u, 1200u));

  assert(feed_line(
           &runtime,
           "LC1,camera_bridge,presence_demo,1,presence_confidence,82",
           1000u, &result) == LC_ACCESSORY_RUNTIME_GREETING);
  assert((result.actions & LC_ACTION_PLAY_GREETING) != 0u);
  assert(strcmp(result.ack,
                "LC1ACK,camera_bridge,presence_demo,1,ok") == 0);

  assert(lc_accessory_runtime_finish_greeting(&runtime, 1800u) ==
         LC_ACTION_SHOW_STATE);
  assert(state.state == LC_STATE_IDLE);

  assert(feed_line(
           &runtime,
           "LC1,camera_bridge,presence_demo,1,presence_confidence,82",
           1900u, &result) == LC_ACCESSORY_RUNTIME_REJECTED);
  assert(strcmp(result.ack,
                "LC1ACK,camera_bridge,presence_demo,1,duplicate") == 0);

  assert(feed_line(
           &runtime,
           "LC1,camera_bridge,presence_demo,2,presence_confidence,0",
           3000u, &result) == LC_ACCESSORY_RUNTIME_ACCEPTED);
  assert(result.actions == LC_ACTION_NONE);

  assert(feed_line(
           &runtime,
           "LC1,camera_bridge,presence_demo,3,presence_confidence,90",
           32000u, &result) == LC_ACCESSORY_RUNTIME_GREETING);
}

static void test_low_confidence_and_non_presence_are_safe(void)
{
  lc_state_machine_t state;
  lc_accessory_runtime_t runtime;
  lc_accessory_runtime_result_t result;

  lc_state_init(&state, 30000u);
  assert(lc_accessory_runtime_init(&runtime, &state,
                                   LC_PRESENCE_SOURCE_CAMERA, 70u, 1200u));

  assert(feed_line(
           &runtime,
           "LC1,camera_bridge,presence_demo,1,presence_confidence,65",
           1000u, &result) == LC_ACCESSORY_RUNTIME_ACCEPTED);
  assert(result.actions == LC_ACTION_NONE);
  assert(state.state == LC_STATE_IDLE);

  assert(feed_line(&runtime,
                   "LC1,camera_bridge,presence_demo,2,heartbeat,0",
                   1100u, &result) == LC_ACCESSORY_RUNTIME_ACCEPTED);
  assert(result.actions == LC_ACTION_NONE);
}

static void test_validated_remote_event_uses_same_replay_and_greeting_path(void)
{
  lc_state_machine_t state;
  lc_accessory_runtime_t runtime;
  lc_accessory_runtime_result_t result;
  lc_accessory_event_t event;

  lc_state_init(&state, 30000u);
  assert(lc_accessory_runtime_init(&runtime, &state,
                                   LC_PRESENCE_SOURCE_MMWAVE, 70u, 1200u));
  assert(lc_accessory_event_parse(
           "LC1,mmwave_bridge,radar-a,1,presence,1", &event) ==
         LC_ACCESSORY_OK);
  assert(lc_accessory_runtime_accept(
           &runtime, &event, 1000u, &result) ==
         LC_ACCESSORY_RUNTIME_GREETING);
  assert((result.actions & LC_ACTION_PLAY_GREETING) != 0u);
  assert(strcmp(result.ack,
                "LC1ACK,mmwave_bridge,radar-a,1,ok") == 0);

  assert(lc_accessory_runtime_accept(
           &runtime, &event, 1100u, &result) ==
         LC_ACCESSORY_RUNTIME_REJECTED);
  assert(strcmp(result.ack,
                "LC1ACK,mmwave_bridge,radar-a,1,duplicate") == 0);
  assert(lc_accessory_runtime_accept(NULL, &event, 1200u, &result) ==
         LC_ACCESSORY_RUNTIME_INVALID);
  assert(lc_accessory_runtime_accept(&runtime, NULL, 1200u, &result) ==
         LC_ACCESSORY_RUNTIME_INVALID);
}

static void test_direct_local_presence_has_no_fabricated_wire_ack(void)
{
  lc_state_machine_t state;
  lc_accessory_runtime_t runtime;
  lc_accessory_runtime_result_t result;

  lc_state_init(&state, 30000u);
  assert(lc_accessory_runtime_init(&runtime, &state,
                                   LC_PRESENCE_SOURCE_MMWAVE, 70u, 1200u));
  assert(lc_accessory_runtime_accept_binary(
           &runtime, true, 1000u, &result) ==
         LC_ACCESSORY_RUNTIME_GREETING);
  assert((result.actions & LC_ACTION_PLAY_GREETING) != 0u);
  assert(result.ack[0] == '\0');
  assert(result.event.type == LC_ACCESSORY_PRESENCE);
  assert(result.event.value == 1);

  assert(lc_accessory_runtime_finish_greeting(&runtime, 1800u) ==
         LC_ACTION_SHOW_STATE);
  assert(lc_accessory_runtime_accept_binary(
           &runtime, true, 2000u, &result) ==
         LC_ACCESSORY_RUNTIME_ACCEPTED);
  assert(result.actions == LC_ACTION_NONE);
  assert(lc_accessory_runtime_accept_binary(
           &runtime, false, 4000u, &result) ==
         LC_ACCESSORY_RUNTIME_ACCEPTED);
  assert(result.event.value == 0);
  assert(lc_accessory_runtime_accept_binary(
           &runtime, true, 32000u, &result) ==
         LC_ACCESSORY_RUNTIME_GREETING);

  assert(lc_accessory_runtime_accept_binary(NULL, true, 33000u, &result) ==
         LC_ACCESSORY_RUNTIME_INVALID);
  assert(lc_accessory_runtime_accept_binary(&runtime, true, 33000u, NULL) ==
         LC_ACCESSORY_RUNTIME_INVALID);
}

int main(void)
{
  test_continuous_camera_presence_and_replay();
  test_low_confidence_and_non_presence_are_safe();
  test_validated_remote_event_uses_same_replay_and_greeting_path();
  test_direct_local_presence_has_no_fabricated_wire_ack();
  puts("PASS: lc_accessory_runtime");
  return 0;
}
