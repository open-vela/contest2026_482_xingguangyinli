#ifndef LIVING_CANVAS_LC_STATE_H
#define LIVING_CANVAS_LC_STATE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  LC_STATE_IDLE = 0,
  LC_STATE_GREETING,
  LC_STATE_LISTENING,
  LC_STATE_THINKING,
  LC_STATE_RECOMMENDATION,
  LC_STATE_CONFIRMED,
  LC_STATE_ERROR
} lc_state_t;

typedef enum
{
  LC_EVENT_PRESENCE = 0,
  LC_EVENT_USER_START,
  LC_EVENT_INPUT_READY,
  LC_EVENT_AI_RESULT,
  LC_EVENT_CONFIRM,
  LC_EVENT_CANCEL,
  LC_EVENT_NETWORK_ERROR,
  LC_EVENT_GREETING_FINISHED
} lc_event_t;

typedef uint32_t lc_actions_t;

typedef enum
{
  LC_ACTION_NONE = 0u,
  LC_ACTION_SHOW_STATE = 1u << 0,
  LC_ACTION_START_CAPTURE = 1u << 1,
  LC_ACTION_REQUEST_DINNER = 1u << 2,
  LC_ACTION_SET_LIGHT = 1u << 3,
  LC_ACTION_SHOW_QR = 1u << 4,
  LC_ACTION_SAVE_CONFIRMED_MEMORY = 1u << 5,
  LC_ACTION_PLAY_GREETING = 1u << 6
} lc_action_t;

typedef struct
{
  lc_state_t state;
  uint64_t last_greeting_ms;
  uint32_t presence_cooldown_ms;
  bool has_greeted;
} lc_state_machine_t;

void lc_state_init(lc_state_machine_t *machine, uint32_t presence_cooldown_ms);
lc_actions_t lc_state_handle(lc_state_machine_t *machine,
                             lc_event_t event,
                             uint64_t now_ms);

#endif
