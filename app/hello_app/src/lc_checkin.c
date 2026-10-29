#include "lc_checkin.h"

#include <stdio.h>
#include <string.h>

static void reset_result(lc_checkin_result_t *result)
{
  memset(result, 0, sizeof(*result));
  result->status = LC_CHECKIN_IGNORED;
  result->slot = LC_CHECKIN_SLOT_COUNT;
}

static bool device_slot(const char *device_id, lc_checkin_slot_t *slot)
{
  if (strcmp(device_id, "cat_a") == 0)
    {
      *slot = LC_CHECKIN_CAT_A;
      return true;
    }

  if (strcmp(device_id, "cat_b") == 0)
    {
      *slot = LC_CHECKIN_CAT_B;
      return true;
    }

  return false;
}

void lc_checkin_tracker_init(lc_checkin_tracker_t *tracker)
{
  size_t index;

  if (tracker == NULL)
    {
      return;
    }

  memset(tracker, 0, sizeof(*tracker));
  for (index = 0u; index < LC_CHECKIN_SLOT_COUNT; index++)
    {
      lc_accessory_cursor_init(&tracker->cursor[index]);
    }
  tracker->initialized = true;
}

lc_checkin_status_t lc_checkin_tracker_accept(
  lc_checkin_tracker_t *tracker,
  const lc_accessory_event_t *event,
  lc_checkin_result_t *result)
{
  lc_accessory_accept_status_t acceptance;
  lc_checkin_slot_t slot;

  if (result == NULL)
    {
      return LC_CHECKIN_INVALID;
    }

  reset_result(result);
  if (tracker == NULL || !tracker->initialized || event == NULL)
    {
      result->status = LC_CHECKIN_INVALID;
      return result->status;
    }

  if (event->type != LC_ACCESSORY_CHECKIN)
    {
      return result->status;
    }

  if (!device_slot(event->device_id, &slot))
    {
      if (!lc_accessory_ack_format(event, LC_ACCESSORY_DEVICE_MISMATCH,
                                   result->ack, sizeof(result->ack)))
        {
          result->status = LC_CHECKIN_INVALID;
          return result->status;
        }
      result->status = LC_CHECKIN_REJECTED;
      return result->status;
    }

  result->slot = slot;
  acceptance = lc_accessory_cursor_accept(&tracker->cursor[slot], event);
  if (!lc_accessory_ack_format(event, acceptance,
                               result->ack, sizeof(result->ack)))
    {
      result->status = LC_CHECKIN_INVALID;
      return result->status;
    }

  if (acceptance == LC_ACCESSORY_ACCEPTED ||
      acceptance == LC_ACCESSORY_NEW_SESSION)
    {
      tracker->checked_in[slot] = true;
      result->status = LC_CHECKIN_ACCEPTED;
    }
  else if (acceptance == LC_ACCESSORY_REPLAY)
    {
      result->status = LC_CHECKIN_DUPLICATE;
    }
  else
    {
      result->status = LC_CHECKIN_REJECTED;
    }

  return result->status;
}

bool lc_checkin_tracker_checked_in(const lc_checkin_tracker_t *tracker,
                                   lc_checkin_slot_t slot)
{
  return tracker != NULL && tracker->initialized &&
         slot < LC_CHECKIN_SLOT_COUNT && tracker->checked_in[slot];
}

bool lc_checkin_format_status(const lc_checkin_tracker_t *tracker,
                              char *buffer,
                              size_t buffer_size)
{
  int written;

  if (tracker == NULL || !tracker->initialized || buffer == NULL ||
      buffer_size == 0u)
    {
      return false;
    }

  written = snprintf(buffer, buffer_size, "A%s打卡 · B%s打卡",
                     tracker->checked_in[LC_CHECKIN_CAT_A] ? "已" : "未",
                     tracker->checked_in[LC_CHECKIN_CAT_B] ? "已" : "未");
  if (written < 0 || (size_t)written >= buffer_size)
    {
      buffer[0] = '\0';
      return false;
    }

  return true;
}

bool lc_checkin_runtime_init(lc_checkin_runtime_t *runtime)
{
  if (runtime == NULL)
    {
      return false;
    }

  memset(runtime, 0, sizeof(*runtime));
  lc_accessory_stream_init(&runtime->stream);
  lc_checkin_tracker_init(&runtime->tracker);
  runtime->initialized = true;
  return true;
}

lc_checkin_status_t lc_checkin_runtime_feed(
  lc_checkin_runtime_t *runtime,
  uint8_t byte,
  lc_checkin_result_t *result)
{
  lc_accessory_event_t event;
  lc_accessory_stream_status_t stream_status;

  if (result == NULL)
    {
      return LC_CHECKIN_INVALID;
    }

  reset_result(result);
  if (runtime == NULL || !runtime->initialized)
    {
      result->status = LC_CHECKIN_INVALID;
      return result->status;
    }

  stream_status = lc_accessory_stream_feed(&runtime->stream, byte, &event);
  if (stream_status == LC_ACCESSORY_STREAM_NONE)
    {
      return result->status;
    }

  if (stream_status != LC_ACCESSORY_STREAM_EVENT)
    {
      result->status = LC_CHECKIN_INVALID;
      return result->status;
    }

  return lc_checkin_tracker_accept(&runtime->tracker, &event, result);
}
