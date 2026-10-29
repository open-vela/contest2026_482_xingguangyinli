#include "lc_state.h"

#include <stddef.h>

void lc_state_init(lc_state_machine_t *machine, uint32_t presence_cooldown_ms)
{
  if (machine == NULL)
    {
      return;
    }

  machine->state = LC_STATE_IDLE;
  machine->last_greeting_ms = 0u;
  machine->presence_cooldown_ms = presence_cooldown_ms;
  machine->has_greeted = false;
}

lc_actions_t lc_state_handle(lc_state_machine_t *machine,
                             lc_event_t event,
                             uint64_t now_ms)
{
  if (machine == NULL)
    {
      return LC_ACTION_NONE;
    }

  if (event == LC_EVENT_NETWORK_ERROR)
    {
      machine->state = LC_STATE_ERROR;
      return LC_ACTION_SHOW_STATE;
    }

  if (event == LC_EVENT_CANCEL)
    {
      if (machine->state == LC_STATE_IDLE ||
          machine->state == LC_STATE_CONFIRMED)
        {
          return LC_ACTION_NONE;
        }

      machine->state = LC_STATE_IDLE;
      return LC_ACTION_SHOW_STATE;
    }

  switch (machine->state)
    {
      case LC_STATE_IDLE:
        if (event == LC_EVENT_PRESENCE)
          {
            if (machine->has_greeted)
              {
                if (now_ms < machine->last_greeting_ms)
                  {
                    machine->last_greeting_ms = now_ms;
                    return LC_ACTION_NONE;
                  }

                if (now_ms - machine->last_greeting_ms <
                    machine->presence_cooldown_ms)
                  {
                    return LC_ACTION_NONE;
                  }
              }

            machine->state = LC_STATE_GREETING;
            machine->last_greeting_ms = now_ms;
            machine->has_greeted = true;
            return LC_ACTION_SHOW_STATE | LC_ACTION_PLAY_GREETING;
          }
        break;

      case LC_STATE_GREETING:
        if (event == LC_EVENT_GREETING_FINISHED)
          {
            machine->state = LC_STATE_IDLE;
            return LC_ACTION_SHOW_STATE;
          }
        else if (event == LC_EVENT_USER_START)
          {
            machine->state = LC_STATE_LISTENING;
            return LC_ACTION_SHOW_STATE | LC_ACTION_START_CAPTURE;
          }
        break;

      case LC_STATE_LISTENING:
        if (event == LC_EVENT_INPUT_READY)
          {
            machine->state = LC_STATE_THINKING;
            return LC_ACTION_SHOW_STATE | LC_ACTION_REQUEST_DINNER;
          }
        break;

      case LC_STATE_THINKING:
        if (event == LC_EVENT_AI_RESULT)
          {
            machine->state = LC_STATE_RECOMMENDATION;
            return LC_ACTION_SHOW_STATE;
          }
        break;

      case LC_STATE_RECOMMENDATION:
        if (event == LC_EVENT_CONFIRM)
          {
            machine->state = LC_STATE_CONFIRMED;
            return LC_ACTION_SHOW_STATE | LC_ACTION_SHOW_QR |
                   LC_ACTION_SET_LIGHT |
                   LC_ACTION_SAVE_CONFIRMED_MEMORY;
          }
        break;

      case LC_STATE_CONFIRMED:
      case LC_STATE_ERROR:
      default:
        break;
    }

  return LC_ACTION_NONE;
}
