#include "lc_presence_flow.h"

#include <assert.h>
#include <stdio.h>

static lc_presence_observation_t sample(bool detected, uint64_t now_ms)
{
  lc_presence_observation_t observation;

  assert(lc_presence_from_binary(LC_PRESENCE_SOURCE_MMWAVE, detected,
                                 now_ms, &observation));
  return observation;
}
static void test_enter_dispatches_one_greeting_action(void)
{
  lc_presence_tracker_t tracker;
  lc_state_machine_t state;
  lc_presence_observation_t enter = sample(true, 1000u);
  lc_presence_observation_t stay = sample(true, 1200u);
  lc_actions_t actions;

  lc_presence_tracker_init(&tracker, 70u, 500u);
  lc_state_init(&state, 30000u);
  actions = lc_presence_flow_handle(&tracker, &state, &enter);
  assert((actions & LC_ACTION_PLAY_GREETING) != 0u);
  assert(state.state == LC_STATE_GREETING);
  assert(lc_presence_flow_handle(&tracker, &state, &stay) ==
         LC_ACTION_NONE);
}
static void test_reentry_respects_existing_greeting_cooldown(void)
{
  lc_presence_tracker_t tracker;
  lc_state_machine_t state;
  lc_presence_observation_t enter = sample(true, 1000u);
  lc_presence_observation_t leave = sample(false, 1500u);
  lc_presence_observation_t early_reentry = sample(true, 2000u);
  lc_presence_observation_t second_leave = sample(false, 2500u);
  lc_presence_observation_t late_reentry = sample(true, 31000u);
  lc_actions_t actions;

  lc_presence_tracker_init(&tracker, 70u, 500u);
  lc_state_init(&state, 30000u);
  assert((lc_presence_flow_handle(&tracker, &state, &enter) &
          LC_ACTION_PLAY_GREETING) != 0u);
  (void)lc_state_handle(&state, LC_EVENT_CANCEL, 1100u);
  assert(lc_presence_flow_handle(&tracker, &state, &leave) ==
         LC_ACTION_NONE);
  assert(lc_presence_flow_handle(&tracker, &state, &early_reentry) ==
         LC_ACTION_NONE);
  assert(state.state == LC_STATE_IDLE);
  assert(lc_presence_flow_handle(&tracker, &state, &second_leave) ==
         LC_ACTION_NONE);
  actions = lc_presence_flow_handle(&tracker, &state, &late_reentry);
  assert((actions & LC_ACTION_PLAY_GREETING) != 0u);
  assert(state.state == LC_STATE_GREETING);
}

int main(void)
{
  test_enter_dispatches_one_greeting_action();
  test_reentry_respects_existing_greeting_cooldown();
  puts("PASS: lc_presence_flow");
  return 0;
}
