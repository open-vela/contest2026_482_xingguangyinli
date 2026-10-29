#ifndef LIVING_CANVAS_LC_ACCESSORY_EVENT_H
#define LIVING_CANVAS_LC_ACCESSORY_EVENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LC_ACCESSORY_ID_MAX 24u
#define LC_ACCESSORY_WIRE_MAX 128u

typedef enum
{
  LC_ACCESSORY_SHAKE = 0,
  LC_ACCESSORY_CONFIRM,
  LC_ACCESSORY_CANCEL,
  LC_ACCESSORY_HEARTBEAT,
  LC_ACCESSORY_CHECKIN,
  LC_ACCESSORY_PRESENCE,
  LC_ACCESSORY_PRESENCE_CONFIDENCE
} lc_accessory_event_type_t;

typedef enum
{
  LC_ACCESSORY_OK = 0,
  LC_ACCESSORY_INVALID,
  LC_ACCESSORY_MALFORMED,
  LC_ACCESSORY_BAD_VERSION,
  LC_ACCESSORY_BAD_ID,
  LC_ACCESSORY_BAD_SEQUENCE,
  LC_ACCESSORY_BAD_TYPE,
  LC_ACCESSORY_BAD_VALUE
} lc_accessory_parse_status_t;

typedef enum
{
  LC_ACCESSORY_ACCEPTED = 0,
  LC_ACCESSORY_NEW_SESSION,
  LC_ACCESSORY_REPLAY,
  LC_ACCESSORY_DEVICE_MISMATCH,
  LC_ACCESSORY_CURSOR_INVALID
} lc_accessory_accept_status_t;

typedef struct
{
  char device_id[LC_ACCESSORY_ID_MAX];
  char session_id[LC_ACCESSORY_ID_MAX];
  uint32_t sequence;
  lc_accessory_event_type_t type;
  int32_t value;
} lc_accessory_event_t;

typedef struct
{
  char device_id[LC_ACCESSORY_ID_MAX];
  char session_id[LC_ACCESSORY_ID_MAX];
  uint32_t last_sequence;
  bool valid;
} lc_accessory_cursor_t;

lc_accessory_parse_status_t lc_accessory_event_parse(
  const char *wire,
  lc_accessory_event_t *event);
void lc_accessory_cursor_init(lc_accessory_cursor_t *cursor);
lc_accessory_accept_status_t lc_accessory_cursor_accept(
  lc_accessory_cursor_t *cursor,
  const lc_accessory_event_t *event);
bool lc_accessory_ack_format(
  const lc_accessory_event_t *event,
  lc_accessory_accept_status_t status,
  char *buffer,
  size_t buffer_size);

#endif
