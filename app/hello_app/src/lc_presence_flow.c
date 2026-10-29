#include "lc_presence_flow.h"

#include <stddef.h>

lc_actions_t lc_presence_flow_handle(
  lc_presence_tracker_t *tracker,
  lc_state_machine_t *state,
  const lc_presence_observation_t *observation)
{
  lc_presence_event_t event;

  if (tracker == NULL || state == NULL || observation == NULL)
    {
      return LC_ACTION_NONE;
    }

  event = lc_presence_update(tracker, observation);
  if (event != LC_PRESENCE_ENTER)
    {
      return LC_ACTION_NONE;
    }

  return lc_state_handle(state, LC_EVENT_PRESENCE,
                         observation->observed_ms);
}
