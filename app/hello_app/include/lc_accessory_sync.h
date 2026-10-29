#ifndef LIVING_CANVAS_LC_ACCESSORY_SYNC_H
#define LIVING_CANVAS_LC_ACCESSORY_SYNC_H

#include "lc_accessory_client.h"
#include "lc_accessory_runtime.h"
#include "lc_checkin.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LC_ACCESSORY_SYNC_STATUS_MAX 48u

typedef struct
{
  lc_accessory_client_status_t client_status;
  lc_actions_t actions;
  size_t event_count;
  bool greeting_requested;
  bool checkin_changed;
  bool history_truncated;
  bool server_reset;
  char checkin_status[LC_ACCESSORY_SYNC_STATUS_MAX];
} lc_accessory_sync_result_t;

typedef struct
{
  lc_accessory_client_t *client;
  lc_state_machine_t presence_state;
  lc_accessory_runtime_t presence_runtime;
  lc_checkin_tracker_t checkins;
  bool initialized;
} lc_accessory_sync_t;

/* Coordinates one authenticated, bounded relay poll. It owns no network
 * thread and performs no retry; the display worker controls scheduling and
 * backoff. */
bool lc_accessory_sync_init(lc_accessory_sync_t *sync,
                            lc_accessory_client_t *client);
lc_accessory_client_status_t lc_accessory_sync_poll(
  lc_accessory_sync_t *sync,
  uint64_t observed_ms,
  lc_accessory_sync_result_t *result);
lc_actions_t lc_accessory_sync_finish_greeting(lc_accessory_sync_t *sync,
                                                uint64_t observed_ms);

#endif
