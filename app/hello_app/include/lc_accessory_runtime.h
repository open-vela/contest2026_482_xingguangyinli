#ifndef LIVING_CANVAS_LC_ACCESSORY_RUNTIME_H
#define LIVING_CANVAS_LC_ACCESSORY_RUNTIME_H

#include "lc_accessory_stream.h"
#include "lc_presence_flow.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  LC_ACCESSORY_RUNTIME_NONE = 0,
  LC_ACCESSORY_RUNTIME_ACCEPTED,
  LC_ACCESSORY_RUNTIME_GREETING,
  LC_ACCESSORY_RUNTIME_DROPPED,
  LC_ACCESSORY_RUNTIME_INVALID,
  LC_ACCESSORY_RUNTIME_REJECTED
} lc_accessory_runtime_status_t;

typedef struct
{
  lc_accessory_runtime_status_t status;
  lc_accessory_event_t event;
  lc_accessory_accept_status_t acceptance;
  lc_actions_t actions;
  char ack[LC_ACCESSORY_WIRE_MAX];
} lc_accessory_runtime_result_t;

typedef struct
{
  lc_accessory_stream_t stream;
  lc_accessory_cursor_t cursor;
  lc_presence_tracker_t presence;
  lc_state_machine_t *state;
  lc_presence_source_t source;
  bool initialized;
} lc_accessory_runtime_t;

bool lc_accessory_runtime_init(lc_accessory_runtime_t *runtime,
                               lc_state_machine_t *state,
                               lc_presence_source_t source,
                               uint8_t minimum_confidence,
                               uint32_t leave_grace_ms);
lc_accessory_runtime_status_t lc_accessory_runtime_feed(
  lc_accessory_runtime_t *runtime,
  uint8_t byte,
  uint64_t observed_ms,
  lc_accessory_runtime_result_t *result);
/* Accepts an event already validated by lc_accessory_event_parse (including
 * events decoded by the authenticated backend client). The same cursor,
 * replay and greeting rules as the byte-stream path are applied. */
lc_accessory_runtime_status_t lc_accessory_runtime_accept(
  lc_accessory_runtime_t *runtime,
  const lc_accessory_event_t *event,
  uint64_t observed_ms,
  lc_accessory_runtime_result_t *result);
/* Accepts a fresh boolean from a directly attached, protocol-specific parser.
 * It does not fabricate a remote session, sequence number or wire ACK. */
lc_accessory_runtime_status_t lc_accessory_runtime_accept_binary(
  lc_accessory_runtime_t *runtime,
  bool detected,
  uint64_t observed_ms,
  lc_accessory_runtime_result_t *result);
lc_actions_t lc_accessory_runtime_finish_greeting(
  lc_accessory_runtime_t *runtime,
  uint64_t observed_ms);

#endif
