#ifndef LIVING_CANVAS_LC_PRESENCE_H
#define LIVING_CANVAS_LC_PRESENCE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  LC_PRESENCE_SOURCE_ACCESSORY = 0,
  LC_PRESENCE_SOURCE_MMWAVE,
  LC_PRESENCE_SOURCE_CAMERA
} lc_presence_source_t;

typedef enum
{
  LC_PRESENCE_NONE = 0,
  LC_PRESENCE_ENTER,
  LC_PRESENCE_STAY,
  LC_PRESENCE_LEAVE,
  LC_PRESENCE_INVALID
} lc_presence_event_t;

typedef struct
{
  lc_presence_source_t source;
  bool detected;
  uint8_t confidence_percent;
  uint64_t observed_ms;
} lc_presence_observation_t;

typedef struct
{
  bool present;
  bool absence_observed;
  uint8_t enter_confidence_percent;
  uint32_t leave_after_ms;
  uint64_t last_seen_ms;
  lc_presence_source_t last_source;
} lc_presence_tracker_t;

void lc_presence_tracker_init(lc_presence_tracker_t *tracker,
                              uint8_t enter_confidence_percent,
                              uint32_t leave_after_ms);
bool lc_presence_from_binary(lc_presence_source_t source,
                             bool detected,
                             uint64_t observed_ms,
                             lc_presence_observation_t *observation);
lc_presence_event_t lc_presence_update(
  lc_presence_tracker_t *tracker,
  const lc_presence_observation_t *observation);
/* Advance a previously observed absence through the grace period. Silence
 * alone never becomes absence: this only acts after lc_presence_update()
 * accepted an explicit non-present observation. */
lc_presence_event_t lc_presence_tick(lc_presence_tracker_t *tracker,
                                     uint64_t observed_ms);

#endif
