#include "lc_accessory_sync.h"

#include <string.h>

#define LC_ACCESSORY_SYNC_GREETING_COOLDOWN_MS 30000u
#define LC_ACCESSORY_SYNC_MINIMUM_CONFIDENCE 70u
#define LC_ACCESSORY_SYNC_LEAVE_GRACE_MS 1200u

static void reset_result(lc_accessory_sync_result_t *result)
{
  memset(result, 0, sizeof(*result));
  result->client_status = LC_ACCESSORY_CLIENT_INVALID;
}

static void format_checkins(const lc_accessory_sync_t *sync,
                            lc_accessory_sync_result_t *result)
{
  if (sync == NULL || !sync->initialized ||
      !lc_checkin_format_status(&sync->checkins, result->checkin_status,
                                sizeof(result->checkin_status)))
    {
      result->checkin_status[0] = '\0';
    }
}

bool lc_accessory_sync_init(lc_accessory_sync_t *sync,
                            lc_accessory_client_t *client)
{
  if (sync == NULL || client == NULL || !client->configured)
    {
      return false;
    }

  memset(sync, 0, sizeof(*sync));
  sync->client = client;
  lc_state_init(&sync->presence_state,
                LC_ACCESSORY_SYNC_GREETING_COOLDOWN_MS);
  if (!lc_accessory_runtime_init(&sync->presence_runtime,
                                 &sync->presence_state,
                                 LC_PRESENCE_SOURCE_MMWAVE,
                                 LC_ACCESSORY_SYNC_MINIMUM_CONFIDENCE,
                                 LC_ACCESSORY_SYNC_LEAVE_GRACE_MS))
    {
      memset(sync, 0, sizeof(*sync));
      return false;
    }
  lc_checkin_tracker_init(&sync->checkins);
  sync->initialized = true;
  return true;
}

lc_accessory_client_status_t lc_accessory_sync_poll(
  lc_accessory_sync_t *sync,
  uint64_t observed_ms,
  lc_accessory_sync_result_t *result)
{
  lc_accessory_event_t events[LC_ACCESSORY_POLL_MAX];
  lc_accessory_client_status_t client_status;
  size_t event_count = 0u;
  bool truncated = false;
  size_t index;

  if (result == NULL)
    {
      return LC_ACCESSORY_CLIENT_INVALID;
    }
  reset_result(result);
  if (sync == NULL || !sync->initialized || sync->client == NULL ||
      !sync->client->configured)
    {
      return result->client_status;
    }

  client_status = lc_accessory_client_poll(sync->client, events,
                                            LC_ACCESSORY_POLL_MAX,
                                            &event_count, &truncated);
  result->client_status = client_status;
  result->server_reset = client_status == LC_ACCESSORY_CLIENT_RESET;
  result->history_truncated = truncated;
  format_checkins(sync, result);
  if (client_status != LC_ACCESSORY_CLIENT_OK)
    {
      return client_status;
    }

  result->event_count = event_count;
  for (index = 0u; index < event_count; index++)
    {
      if (events[index].type == LC_ACCESSORY_CHECKIN)
        {
          lc_checkin_result_t checkin;
          if (lc_checkin_tracker_accept(&sync->checkins, &events[index],
                                        &checkin) == LC_CHECKIN_ACCEPTED)
            {
              result->checkin_changed = true;
            }
        }
      else if (events[index].type == LC_ACCESSORY_PRESENCE)
        {
          lc_accessory_runtime_result_t presence;
          lc_accessory_runtime_status_t status;

          status = lc_accessory_runtime_accept(&sync->presence_runtime,
                                                &events[index], observed_ms,
                                                &presence);
          if (status == LC_ACCESSORY_RUNTIME_GREETING)
            {
              result->greeting_requested = true;
            }
          if (status == LC_ACCESSORY_RUNTIME_ACCEPTED ||
              status == LC_ACCESSORY_RUNTIME_GREETING)
            {
              result->actions |= presence.actions;
            }
        }
    }

  /* A validated false event may share this poll timestamp with the preceding
   * true event. Preserve the anti-flicker grace period, then finish that
   * pending leave on a later poll even when the sensor emits no repeat. */
  (void)lc_presence_tick(&sync->presence_runtime.presence, observed_ms);
  format_checkins(sync, result);
  return client_status;
}

lc_actions_t lc_accessory_sync_finish_greeting(lc_accessory_sync_t *sync,
                                                uint64_t observed_ms)
{
  if (sync == NULL || !sync->initialized)
    {
      return LC_ACTION_NONE;
    }

  return lc_accessory_runtime_finish_greeting(&sync->presence_runtime,
                                               observed_ms);
}
