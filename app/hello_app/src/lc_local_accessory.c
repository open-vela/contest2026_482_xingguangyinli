#include "lc_local_accessory.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LC_LOCAL_ACCESSORY_REQUEST_FIELDS 7u

static bool device_from_id(const char *device_id,
                           lc_local_accessory_device_t *device)
{
  if (device_id == NULL || device == NULL)
    {
      return false;
    }

  if (strcmp(device_id, "cat_a") == 0)
    {
      *device = LC_LOCAL_ACCESSORY_CAT_A;
      return true;
    }

  if (strcmp(device_id, "cat_b") == 0)
    {
      *device = LC_LOCAL_ACCESSORY_CAT_B;
      return true;
    }

  return false;
}

static bool valid_session(const char *session_id)
{
  size_t index;
  size_t length;

  if (session_id == NULL)
    {
      return false;
    }

  length = strlen(session_id);
  if (length == 0u || length >= LC_LOCAL_ACCESSORY_SESSION_MAX)
    {
      return false;
    }

  for (index = 0u; index < length; index++)
    {
      const char current = session_id[index];
      const bool lower = current >= 'a' && current <= 'z';
      const bool upper = current >= 'A' && current <= 'Z';
      const bool digit = current >= '0' && current <= '9';

      if (!lower && !upper && !digit && current != '_' && current != '-')
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
  size_t index;

  if (field == NULL || sequence == NULL || field[0] < '1' ||
      field[0] > '9')
    {
      return false;
    }

  for (index = 0u; field[index] != '\0'; index++)
    {
      if (field[index] < '0' || field[index] > '9')
        {
          return false;
        }
    }

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

static int lower_hex_value(char value)
{
  if (value >= '0' && value <= '9')
    {
      return value - '0';
    }
  if (value >= 'a' && value <= 'f')
    {
      return value - 'a' + 10;
    }
  return -1;
}

static bool decode_tag(const char *hex,
                       uint8_t output[LC_LOCAL_ACCESSORY_TAG_BYTES])
{
  size_t index;

  if (hex == NULL || output == NULL ||
      strlen(hex) != LC_LOCAL_ACCESSORY_TAG_HEX_LENGTH)
    {
      return false;
    }

  for (index = 0u; index < LC_LOCAL_ACCESSORY_TAG_BYTES; index++)
    {
      const int high = lower_hex_value(hex[index * 2u]);
      const int low = lower_hex_value(hex[index * 2u + 1u]);

      if (high < 0 || low < 0)
        {
          return false;
        }
      output[index] = (uint8_t)((high << 4) | low);
    }

  return true;
}

static void encode_tag(const uint8_t tag[LC_LOCAL_ACCESSORY_TAG_BYTES],
                       char hex[LC_LOCAL_ACCESSORY_TAG_HEX_LENGTH + 1u])
{
  static const char DIGITS[] = "0123456789abcdef";
  size_t index;

  for (index = 0u; index < LC_LOCAL_ACCESSORY_TAG_BYTES; index++)
    {
      hex[index * 2u] = DIGITS[tag[index] >> 4];
      hex[index * 2u + 1u] = DIGITS[tag[index] & 0x0fu];
    }
  hex[LC_LOCAL_ACCESSORY_TAG_HEX_LENGTH] = '\0';
}

static bool format_signed(const char *prefix,
                          const uint8_t key[LC_LOCAL_ACCESSORY_KEY_BYTES],
                          lc_local_accessory_hmac_fn hmac,
                          void *hmac_context,
                          char *buffer,
                          size_t buffer_size)
{
  uint8_t tag[LC_LOCAL_ACCESSORY_TAG_BYTES];
  char tag_hex[LC_LOCAL_ACCESSORY_TAG_HEX_LENGTH + 1u];
  size_t prefix_length;
  int written;

  if (prefix == NULL || key == NULL || hmac == NULL || buffer == NULL ||
      buffer_size == 0u)
    {
      return false;
    }

  prefix_length = strlen(prefix);
  if (prefix_length == 0u ||
      prefix_length + 1u + LC_LOCAL_ACCESSORY_TAG_HEX_LENGTH >= buffer_size)
    {
      buffer[0] = '\0';
      return false;
    }

  if (!hmac(hmac_context, key, (const uint8_t *)prefix, prefix_length, tag))
    {
      buffer[0] = '\0';
      return false;
    }
  encode_tag(tag, tag_hex);
  written = snprintf(buffer, buffer_size, "%s,%s", prefix, tag_hex);
  if (written < 0 || (size_t)written >= buffer_size)
    {
      buffer[0] = '\0';
      return false;
    }

  return true;
}

bool lc_local_accessory_guard_init(
  lc_local_accessory_guard_t *guard,
  const uint8_t cat_a_key[LC_LOCAL_ACCESSORY_KEY_BYTES],
  const uint8_t cat_b_key[LC_LOCAL_ACCESSORY_KEY_BYTES],
  lc_local_accessory_hmac_fn hmac,
  void *hmac_context)
{
  if (guard == NULL || cat_a_key == NULL || cat_b_key == NULL ||
      hmac == NULL)
    {
      return false;
    }

  memset(guard, 0, sizeof(*guard));
  memcpy(guard->cursor[LC_LOCAL_ACCESSORY_CAT_A].key, cat_a_key,
         LC_LOCAL_ACCESSORY_KEY_BYTES);
  memcpy(guard->cursor[LC_LOCAL_ACCESSORY_CAT_B].key, cat_b_key,
         LC_LOCAL_ACCESSORY_KEY_BYTES);
  guard->hmac = hmac;
  guard->hmac_context = hmac_context;
  guard->initialized = true;
  return true;
}

bool lc_local_accessory_request_format(
  const char *device_id,
  const char *session_id,
  uint32_t sequence,
  const uint8_t key[LC_LOCAL_ACCESSORY_KEY_BYTES],
  lc_local_accessory_hmac_fn hmac,
  void *hmac_context,
  char *buffer,
  size_t buffer_size)
{
  lc_local_accessory_device_t device;
  char prefix[LC_LOCAL_ACCESSORY_PACKET_MAX];
  int written;

  if (!device_from_id(device_id, &device) || !valid_session(session_id) ||
      sequence == 0u || key == NULL || hmac == NULL || buffer == NULL ||
      buffer_size == 0u)
    {
      return false;
    }
  (void)device;

  written = snprintf(prefix, sizeof(prefix), "LC2,%s,%s,%lu,checkin,1",
                     device_id, session_id, (unsigned long)sequence);
  if (written < 0 || (size_t)written >= sizeof(prefix))
    {
      return false;
    }

  return format_signed(prefix, key, hmac, hmac_context,
                       buffer, buffer_size);
}

bool lc_local_accessory_constant_time_equal(const uint8_t *left,
                                             const uint8_t *right,
                                             size_t length)
{
  size_t index;
  uint8_t difference = 0u;

  if (left == NULL || right == NULL)
    {
      return false;
    }

  for (index = 0u; index < length; index++)
    {
      difference |= (uint8_t)(left[index] ^ right[index]);
    }

  return difference == 0u;
}

lc_local_accessory_status_t lc_local_accessory_verify(
  lc_local_accessory_guard_t *guard,
  const uint8_t *packet,
  size_t packet_length,
  lc_local_accessory_event_t *event)
{
  char copy[LC_LOCAL_ACCESSORY_PACKET_MAX];
  char *fields[LC_LOCAL_ACCESSORY_REQUEST_FIELDS];
  char *cursor;
  lc_local_accessory_device_t device;
  lc_local_accessory_cursor_t *device_cursor;
  uint32_t sequence;
  uint8_t received_tag[LC_LOCAL_ACCESSORY_TAG_BYTES];
  uint8_t expected_tag[LC_LOCAL_ACCESSORY_TAG_BYTES];
  size_t field_index;
  size_t prefix_length;

  if (guard == NULL || !guard->initialized || packet == NULL ||
      event == NULL)
    {
      return LC_LOCAL_ACCESSORY_INVALID_ARGUMENT;
    }
  if (packet_length == 0u || packet_length >= sizeof(copy) ||
      memchr(packet, '\0', packet_length) != NULL)
    {
      return LC_LOCAL_ACCESSORY_MALFORMED;
    }

  memcpy(copy, packet, packet_length);
  copy[packet_length] = '\0';
  cursor = copy;
  prefix_length = 0u;
  for (field_index = 0u; field_index < LC_LOCAL_ACCESSORY_REQUEST_FIELDS;
       field_index++)
    {
      char *separator;

      fields[field_index] = cursor;
      separator = strchr(cursor, ',');
      if (field_index + 1u < LC_LOCAL_ACCESSORY_REQUEST_FIELDS)
        {
          if (separator == NULL)
            {
              return LC_LOCAL_ACCESSORY_MALFORMED;
            }
          if (field_index + 2u == LC_LOCAL_ACCESSORY_REQUEST_FIELDS)
            {
              prefix_length = (size_t)(separator - copy);
            }
          *separator = '\0';
          cursor = separator + 1;
        }
      else if (separator != NULL)
        {
          return LC_LOCAL_ACCESSORY_MALFORMED;
        }
    }

  if (strcmp(fields[0], "LC2") != 0 ||
      !device_from_id(fields[1], &device) ||
      !valid_session(fields[2]) ||
      !parse_sequence(fields[3], &sequence) ||
      strcmp(fields[4], "checkin") != 0 ||
      strcmp(fields[5], "1") != 0 ||
      !decode_tag(fields[6], received_tag) || prefix_length == 0u)
    {
      return LC_LOCAL_ACCESSORY_MALFORMED;
    }

  device_cursor = &guard->cursor[device];
  if (!guard->hmac(guard->hmac_context, device_cursor->key,
                   packet, prefix_length, expected_tag))
    {
      return LC_LOCAL_ACCESSORY_CRYPTO_FAILED;
    }
  if (!lc_local_accessory_constant_time_equal(received_tag, expected_tag,
                                               sizeof(received_tag)))
    {
      return LC_LOCAL_ACCESSORY_AUTH_FAILED;
    }

  memset(event, 0, sizeof(*event));
  event->device = device;
  memcpy(event->device_id, fields[1], strlen(fields[1]) + 1u);
  memcpy(event->session_id, fields[2], strlen(fields[2]) + 1u);
  event->sequence = sequence;

  if (device_cursor->valid &&
      strcmp(device_cursor->session_id, event->session_id) == 0 &&
      event->sequence <= device_cursor->last_sequence)
    {
      return LC_LOCAL_ACCESSORY_DUPLICATE;
    }

  memcpy(device_cursor->session_id, event->session_id,
         strlen(event->session_id) + 1u);
  device_cursor->last_sequence = event->sequence;
  device_cursor->valid = true;
  return LC_LOCAL_ACCESSORY_ACCEPTED;
}

bool lc_local_accessory_ack_format(
  const lc_local_accessory_guard_t *guard,
  const lc_local_accessory_event_t *event,
  lc_local_accessory_status_t status,
  char *buffer,
  size_t buffer_size)
{
  const char *wire_status;
  const lc_local_accessory_cursor_t *device_cursor;
  lc_local_accessory_device_t parsed_device;
  char prefix[LC_LOCAL_ACCESSORY_PACKET_MAX];
  int written;

  if (guard == NULL || !guard->initialized || event == NULL ||
      buffer == NULL || buffer_size == 0u ||
      !device_from_id(event->device_id, &parsed_device) ||
      parsed_device != event->device || !valid_session(event->session_id) ||
      event->sequence == 0u)
    {
      return false;
    }

  if (status == LC_LOCAL_ACCESSORY_ACCEPTED)
    {
      wire_status = "accepted";
    }
  else if (status == LC_LOCAL_ACCESSORY_DUPLICATE)
    {
      wire_status = "duplicate";
    }
  else
    {
      return false;
    }

  written = snprintf(prefix, sizeof(prefix), "LC2ACK,%s,%s,%lu,%s",
                     event->device_id, event->session_id,
                     (unsigned long)event->sequence, wire_status);
  if (written < 0 || (size_t)written >= sizeof(prefix))
    {
      return false;
    }

  device_cursor = &guard->cursor[event->device];
  return format_signed(prefix, device_cursor->key, guard->hmac,
                       guard->hmac_context, buffer, buffer_size);
}
