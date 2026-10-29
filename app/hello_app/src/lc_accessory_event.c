#include "lc_accessory_event.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LC_ACCESSORY_FIELD_COUNT 6u

static bool valid_id(const char *value)
{
  size_t index;
  size_t length;

  if (value == NULL)
    {
      return false;
    }

  length = strlen(value);
  if (length == 0u || length >= LC_ACCESSORY_ID_MAX)
    {
      return false;
    }

  for (index = 0u; index < length; index++)
    {
      char current = value[index];
      bool alpha = (current >= 'a' && current <= 'z') ||
                   (current >= 'A' && current <= 'Z');
      bool digit = current >= '0' && current <= '9';

      if (!alpha && !digit && current != '_' && current != '-')
        {
          return false;
        }
    }

  return true;
}

static bool parse_sequence(const char *field, uint32_t *sequence)
{
  char *end;
  unsigned long parsed;

  errno = 0;
  end = NULL;
  parsed = strtoul(field, &end, 10);
  if (errno != 0 || end == field || *end != '\0' || parsed == 0u ||
      parsed > UINT32_MAX)
    {
      return false;
    }

  *sequence = (uint32_t)parsed;
  return true;
}

static bool parse_value(const char *field, int32_t *value)
{
  char *end;
  long parsed;

  errno = 0;
  end = NULL;
  parsed = strtol(field, &end, 10);
  if (errno != 0 || end == field || *end != '\0' || parsed < INT32_MIN ||
      parsed > INT32_MAX)
    {
      return false;
    }

  *value = (int32_t)parsed;
  return true;
}

static bool parse_type(const char *field, lc_accessory_event_type_t *type)
{
  if (strcmp(field, "shake") == 0)
    {
      *type = LC_ACCESSORY_SHAKE;
    }
  else if (strcmp(field, "confirm") == 0)
    {
      *type = LC_ACCESSORY_CONFIRM;
    }
  else if (strcmp(field, "cancel") == 0)
    {
      *type = LC_ACCESSORY_CANCEL;
    }
  else if (strcmp(field, "heartbeat") == 0)
    {
      *type = LC_ACCESSORY_HEARTBEAT;
    }
  else if (strcmp(field, "checkin") == 0)
    {
      *type = LC_ACCESSORY_CHECKIN;
    }
  else if (strcmp(field, "presence") == 0)
    {
      *type = LC_ACCESSORY_PRESENCE;
    }
  else if (strcmp(field, "presence_confidence") == 0)
    {
      *type = LC_ACCESSORY_PRESENCE_CONFIDENCE;
    }
  else
    {
      return false;
    }

  return true;
}

static bool valid_value(lc_accessory_event_type_t type, int32_t value)
{
  if (type == LC_ACCESSORY_SHAKE)
    {
      return value >= -1 && value <= 1;
    }

  if (type == LC_ACCESSORY_PRESENCE)
    {
      return value == 0 || value == 1;
    }

  if (type == LC_ACCESSORY_CHECKIN)
    {
      return value == 1;
    }

  if (type == LC_ACCESSORY_PRESENCE_CONFIDENCE)
    {
      return value >= 0 && value <= 100;
    }

  return value == 0;
}

lc_accessory_parse_status_t lc_accessory_event_parse(
  const char *wire,
  lc_accessory_event_t *event)
{
  char copy[LC_ACCESSORY_WIRE_MAX];
  char *fields[LC_ACCESSORY_FIELD_COUNT];
  char *cursor;
  size_t field_index;
  size_t length;

  if (wire == NULL || event == NULL)
    {
      return LC_ACCESSORY_INVALID;
    }

  length = strlen(wire);
  if (length == 0u || length >= sizeof(copy))
    {
      return LC_ACCESSORY_MALFORMED;
    }

  memcpy(copy, wire, length + 1u);
  cursor = copy;
  for (field_index = 0u; field_index < LC_ACCESSORY_FIELD_COUNT;
       field_index++)
    {
      char *separator;

      fields[field_index] = cursor;
      separator = strchr(cursor, ',');
      if (field_index + 1u < LC_ACCESSORY_FIELD_COUNT)
        {
          if (separator == NULL)
            {
              return LC_ACCESSORY_MALFORMED;
            }

          *separator = '\0';
          cursor = separator + 1;
        }
      else if (separator != NULL)
        {
          return LC_ACCESSORY_MALFORMED;
        }
    }

  if (strcmp(fields[0], "LC1") != 0)
    {
      return LC_ACCESSORY_BAD_VERSION;
    }

  if (!valid_id(fields[1]) || !valid_id(fields[2]))
    {
      return LC_ACCESSORY_BAD_ID;
    }

  memset(event, 0, sizeof(*event));
  memcpy(event->device_id, fields[1], strlen(fields[1]) + 1u);
  memcpy(event->session_id, fields[2], strlen(fields[2]) + 1u);
  if (!parse_sequence(fields[3], &event->sequence))
    {
      return LC_ACCESSORY_BAD_SEQUENCE;
    }

  if (!parse_type(fields[4], &event->type))
    {
      return LC_ACCESSORY_BAD_TYPE;
    }

  if (!parse_value(fields[5], &event->value) ||
      !valid_value(event->type, event->value))
    {
      return LC_ACCESSORY_BAD_VALUE;
    }

  return LC_ACCESSORY_OK;
}

void lc_accessory_cursor_init(lc_accessory_cursor_t *cursor)
{
  if (cursor != NULL)
    {
      memset(cursor, 0, sizeof(*cursor));
    }
}

lc_accessory_accept_status_t lc_accessory_cursor_accept(
  lc_accessory_cursor_t *cursor,
  const lc_accessory_event_t *event)
{
  if (cursor == NULL || event == NULL)
    {
      return LC_ACCESSORY_CURSOR_INVALID;
    }

  if (!cursor->valid)
    {
      memcpy(cursor->device_id, event->device_id,
             sizeof(cursor->device_id));
      memcpy(cursor->session_id, event->session_id,
             sizeof(cursor->session_id));
      cursor->last_sequence = event->sequence;
      cursor->valid = true;
      return LC_ACCESSORY_ACCEPTED;
    }

  if (strcmp(cursor->device_id, event->device_id) != 0)
    {
      return LC_ACCESSORY_DEVICE_MISMATCH;
    }

  if (strcmp(cursor->session_id, event->session_id) != 0)
    {
      memcpy(cursor->session_id, event->session_id,
             sizeof(cursor->session_id));
      cursor->last_sequence = event->sequence;
      return LC_ACCESSORY_NEW_SESSION;
    }

  if (event->sequence <= cursor->last_sequence)
    {
      return LC_ACCESSORY_REPLAY;
    }

  cursor->last_sequence = event->sequence;
  return LC_ACCESSORY_ACCEPTED;
}

bool lc_accessory_ack_format(
  const lc_accessory_event_t *event,
  lc_accessory_accept_status_t status,
  char *buffer,
  size_t buffer_size)
{
  const char *wire_status;
  int written;

  if (event == NULL || buffer == NULL || buffer_size == 0u ||
      !valid_id(event->device_id) || !valid_id(event->session_id) ||
      event->sequence == 0u)
    {
      return false;
    }

  if (status == LC_ACCESSORY_ACCEPTED)
    {
      wire_status = "ok";
    }
  else if (status == LC_ACCESSORY_NEW_SESSION)
    {
      wire_status = "session";
    }
  else if (status == LC_ACCESSORY_REPLAY)
    {
      wire_status = "duplicate";
    }
  else
    {
      wire_status = "rejected";
    }

  written = snprintf(buffer, buffer_size, "LC1ACK,%s,%s,%lu,%s",
                     event->device_id, event->session_id,
                     (unsigned long)event->sequence, wire_status);
  if (written < 0 || (size_t)written >= buffer_size)
    {
      buffer[0] = '\0';
      return false;
    }

  return true;
}
