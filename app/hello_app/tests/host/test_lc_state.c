#include "lc_state.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void expect_action(lc_actions_t actions, lc_action_t action)
{
  assert((actions & (lc_actions_t)action) != 0u);
}

static void test_happy_path(void)
{
  lc_state_machine_t machine;
  lc_actions_t actions;

  lc_state_init(&machine, 30000u);
  assert(machine.state == LC_STATE_IDLE);

  actions = lc_state_handle(&machine, LC_EVENT_PRESENCE, 1000u);
  assert(machine.state == LC_STATE_GREETING);
  expect_action(actions, LC_ACTION_SHOW_STATE);
  expect_action(actions, LC_ACTION_PLAY_GREETING);

  actions = lc_state_handle(&machine, LC_EVENT_USER_START, 1500u);
  assert(machine.state == LC_STATE_LISTENING);
  expect_action(actions, LC_ACTION_START_CAPTURE);

  actions = lc_state_handle(&machine, LC_EVENT_INPUT_READY, 2000u);
  assert(machine.state == LC_STATE_THINKING);
  expect_action(actions, LC_ACTION_REQUEST_DINNER);

  actions = lc_state_handle(&machine, LC_EVENT_AI_RESULT, 2500u);
  assert(machine.state == LC_STATE_RECOMMENDATION);
  assert((actions & LC_ACTION_SHOW_QR) == 0u);

  actions = lc_state_handle(&machine, LC_EVENT_CONFIRM, 3000u);
  assert(machine.state == LC_STATE_CONFIRMED);
  expect_action(actions, LC_ACTION_SHOW_QR);
  expect_action(actions, LC_ACTION_SET_LIGHT);
  expect_action(actions, LC_ACTION_SAVE_CONFIRMED_MEMORY);
}

static void test_cancel_returns_non_writing_states_to_idle(void)
{
  lc_state_machine_t machine;

  lc_state_init(&machine, 30000u);
  machine.state = LC_STATE_LISTENING;
  expect_action(lc_state_handle(&machine, LC_EVENT_CANCEL, 1000u), LC_ACTION_SHOW_STATE);
  assert(machine.state == LC_STATE_IDLE);

  machine.state = LC_STATE_ERROR;
  lc_state_handle(&machine, LC_EVENT_CANCEL, 2000u);
  assert(machine.state == LC_STATE_IDLE);
}

static void test_confirmed_write_is_not_cancelled(void)
{
  lc_state_machine_t machine;

  lc_state_init(&machine, 30000u);
  machine.state = LC_STATE_CONFIRMED;
  assert(lc_state_handle(&machine, LC_EVENT_CANCEL, 1000u) == LC_ACTION_NONE);
  assert(machine.state == LC_STATE_CONFIRMED);
}

static void test_network_error_and_recovery(void)
{
  lc_state_machine_t machine;

  lc_state_init(&machine, 30000u);
  machine.state = LC_STATE_THINKING;
  expect_action(lc_state_handle(&machine, LC_EVENT_NETWORK_ERROR, 1000u), LC_ACTION_SHOW_STATE);
  assert(machine.state == LC_STATE_ERROR);
  lc_state_handle(&machine, LC_EVENT_CANCEL, 1100u);
  assert(machine.state == LC_STATE_IDLE);
}

static void test_presence_cooldown_suppresses_repeat_greeting(void)
{
  lc_state_machine_t machine;

  lc_state_init(&machine, 30000u);
  lc_state_handle(&machine, LC_EVENT_PRESENCE, 1000u);
  lc_state_handle(&machine, LC_EVENT_CANCEL, 2000u);

  assert(lc_state_handle(&machine, LC_EVENT_PRESENCE, 10000u) == LC_ACTION_NONE);
  assert(machine.state == LC_STATE_IDLE);

  expect_action(lc_state_handle(&machine, LC_EVENT_PRESENCE, 31000u), LC_ACTION_SHOW_STATE);
  assert(machine.state == LC_STATE_GREETING);
}

static void test_greeting_completion_returns_to_idle(void)
{
  lc_state_machine_t machine;

  lc_state_init(&machine, 30000u);
  lc_state_handle(&machine, LC_EVENT_PRESENCE, 1000u);
  assert(machine.state == LC_STATE_GREETING);
  expect_action(lc_state_handle(&machine, LC_EVENT_GREETING_FINISHED, 1800u),
                LC_ACTION_SHOW_STATE);
  assert(machine.state == LC_STATE_IDLE);

  assert(lc_state_handle(&machine, LC_EVENT_GREETING_FINISHED, 1900u) ==
         LC_ACTION_NONE);
  assert(machine.state == LC_STATE_IDLE);
}

static void test_presence_cooldown_survives_clock_rollback(void)
{
  lc_state_machine_t machine;

  lc_state_init(&machine, 30000u);
  lc_state_handle(&machine, LC_EVENT_PRESENCE, 50000u);
  lc_state_handle(&machine, LC_EVENT_CANCEL, 51000u);

  assert(lc_state_handle(&machine, LC_EVENT_PRESENCE, 1000u) ==
         LC_ACTION_NONE);
  assert(machine.state == LC_STATE_IDLE);

  expect_action(lc_state_handle(&machine, LC_EVENT_PRESENCE, 31000u),
                LC_ACTION_SHOW_STATE);
  assert(machine.state == LC_STATE_GREETING);
}

static void test_unrelated_event_does_nothing(void)
{
  lc_state_machine_t machine;

  lc_state_init(&machine, 30000u);
  assert(lc_state_handle(&machine, LC_EVENT_CONFIRM, 1000u) == LC_ACTION_NONE);
  assert(machine.state == LC_STATE_IDLE);
}

int main(void)
{
  test_happy_path();
  test_cancel_returns_non_writing_states_to_idle();
  test_confirmed_write_is_not_cancelled();
  test_network_error_and_recovery();
  test_presence_cooldown_suppresses_repeat_greeting();
  test_greeting_completion_returns_to_idle();
  test_presence_cooldown_survives_clock_rollback();
  test_unrelated_event_does_nothing();
  puts("PASS: lc_state");
  return 0;
}
