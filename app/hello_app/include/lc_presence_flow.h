#ifndef LIVING_CANVAS_LC_PRESENCE_FLOW_H
#define LIVING_CANVAS_LC_PRESENCE_FLOW_H

#include "lc_presence.h"
#include "lc_state.h"

lc_actions_t lc_presence_flow_handle(
  lc_presence_tracker_t *tracker,
  lc_state_machine_t *state,
  const lc_presence_observation_t *observation);

#endif
