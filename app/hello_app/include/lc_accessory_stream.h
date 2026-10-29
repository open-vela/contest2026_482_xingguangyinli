#ifndef LIVING_CANVAS_LC_ACCESSORY_STREAM_H
#define LIVING_CANVAS_LC_ACCESSORY_STREAM_H

#include "lc_accessory_event.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
  LC_ACCESSORY_STREAM_NONE = 0,
  LC_ACCESSORY_STREAM_EVENT,
  LC_ACCESSORY_STREAM_DROPPED,
  LC_ACCESSORY_STREAM_INVALID
} lc_accessory_stream_status_t;

typedef struct
{
  char buffer[LC_ACCESSORY_WIRE_MAX];
  size_t length;
  bool overflow;
} lc_accessory_stream_t;

void lc_accessory_stream_init(lc_accessory_stream_t *stream);
lc_accessory_stream_status_t lc_accessory_stream_feed(
  lc_accessory_stream_t *stream,
  uint8_t byte,
  lc_accessory_event_t *event);

#endif
