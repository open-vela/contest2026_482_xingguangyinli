#include "lc_presence.h"

#include <stddef.h>
#include <string.h>

static bool valid_source(lc_presence_source_t source)
{
  return source == LC_PRESENCE_SOURCE_ACCESSORY ||
         source == LC_PRESENCE_SOURCE_MMWAVE ||
         source == LC_PRESENCE_SOURCE_CAMERA;
}
void lc_presence_tracker_init(lc_presence_tracker_t *tracker,
                              uint8_t enter_confidence_percent,
                              uint32_t leave_after_ms)
{
  if (tracker == NULL)
    {
      return;
    }

  memset(tracker, 0, sizeof(*tracker));
  tracker->enter_confidence_percent =
    enter_confidence_percent <= 100u ? enter_confidence_percent : 100u;
  tracker->leave_after_ms = leave_after_ms;
  tracker->last_source = LC_PRESENCE_SOURCE_ACCESSORY;
}
bool lc_presence_from_binary(lc_presence_source_t source,
                             bool detected,
                             uint64_t observed_ms,
                             lc_presence_observation_t *observation)
{
  if (!valid_source(source) || observation == NULL)
    {
      return false;
    }

  observation->source = source;
  observation->detected = detected;
  observation->confidence_percent = detected ? 100u : 0u;
  observation->observed_ms = observed_ms;
  return true;
}

lc_presence_event_t lc_presence_update(
  lc_presence_tracker_t *tracker,
  const lc_presence_observation_t *observation)
{
  bool confident;

  if (tracker == NULL || observation == NULL ||
      !valid_source(observation->source) ||
      observation->confidence_percent > 100u)
    {
      return LC_PRESENCE_INVALID;
    }

  confident = observation->detected &&
              observation->confidence_percent >=
                tracker->enter_confidence_percent;
  if (confident)
    {
      bool was_present = tracker->present;

      tracker->present = true;
      tracker->absence_observed = false;
      tracker->last_seen_ms = observation->observed_ms;
      tracker->last_source = observation->source;
      return was_present ? LC_PRESENCE_STAY : LC_PRESENCE_ENTER;
    }

  if (!tracker->present)
    {
      tracker->absence_observed = false;
      return LC_PRESENCE_NONE;
    }

  tracker->absence_observed = true;

  if (observation->observed_ms < tracker->last_seen_ms)
    {
      tracker->last_seen_ms = observation->observed_ms;
      return LC_PRESENCE_NONE;
    }

  if (observation->observed_ms - tracker->last_seen_ms >=
      tracker->leave_after_ms)
    {
      tracker->present = false;
      tracker->absence_observed = false;
      return LC_PRESENCE_LEAVE;
    }

  return LC_PRESENCE_NONE;
}

lc_presence_event_t lc_presence_tick(lc_presence_tracker_t *tracker,
                                     uint64_t observed_ms)
{
  if (tracker == NULL)
    {
      return LC_PRESENCE_INVALID;
    }

  if (!tracker->present || !tracker->absence_observed)
    {
      return LC_PRESENCE_NONE;
    }

  if (observed_ms < tracker->last_seen_ms)
    {
      tracker->last_seen_ms = observed_ms;
      return LC_PRESENCE_NONE;
    }

  if (observed_ms - tracker->last_seen_ms < tracker->leave_after_ms)
    {
      return LC_PRESENCE_NONE;
    }

  tracker->present = false;
  tracker->absence_observed = false;
  return LC_PRESENCE_LEAVE;
}
