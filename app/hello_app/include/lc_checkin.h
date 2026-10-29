#ifndef LIVING_CANVAS_LC_CHECKIN_H
#define LIVING_CANVAS_LC_CHECKIN_H

#include "lc_accessory_stream.h"

#include <stdbool.h>

typedef enum
{
  LC_CHECKIN_CAT_A = 0,
  LC_CHECKIN_CAT_B,
  LC_CHECKIN_SLOT_COUNT
} lc_checkin_slot_t;

typedef enum
{
  LC_CHECKIN_IGNORED = 0,
  LC_CHECKIN_ACCEPTED,
  LC_CHECKIN_DUPLICATE,
  LC_CHECKIN_REJECTED,
  LC_CHECKIN_INVALID
} lc_checkin_status_t;

typedef struct
{
  lc_checkin_status_t status;
  lc_checkin_slot_t slot;
  char ack[LC_ACCESSORY_WIRE_MAX];
} lc_checkin_result_t;

typedef struct
{
  lc_accessory_cursor_t cursor[LC_CHECKIN_SLOT_COUNT];
  bool checked_in[LC_CHECKIN_SLOT_COUNT];
  bool initialized;
} lc_checkin_tracker_t;

typedef struct
{
  lc_accessory_stream_t stream;
  lc_checkin_tracker_t tracker;
  bool initialized;
} lc_checkin_runtime_t;

void lc_checkin_tracker_init(lc_checkin_tracker_t *tracker);
lc_checkin_status_t lc_checkin_tracker_accept(
  lc_checkin_tracker_t *tracker,
  const lc_accessory_event_t *event,
  lc_checkin_result_t *result);
bool lc_checkin_tracker_checked_in(const lc_checkin_tracker_t *tracker,
                                   lc_checkin_slot_t slot);
bool lc_checkin_format_status(const lc_checkin_tracker_t *tracker,
                              char *buffer,
                              size_t buffer_size);
bool lc_checkin_runtime_init(lc_checkin_runtime_t *runtime);
lc_checkin_status_t lc_checkin_runtime_feed(
  lc_checkin_runtime_t *runtime,
  uint8_t byte,
  lc_checkin_result_t *result);

#endif
