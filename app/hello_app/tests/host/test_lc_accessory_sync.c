#include "lc_accessory_sync.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char token[] = "accessory_owner_01234567890123456789";
static const char *response_json;
static int http_status = 200;
static int calls;

static int request(void *context, const lc_product_transport_t *backend,
                   const char *path, const char *authorization,
                   char *response, size_t capacity, size_t *response_length)
{
  size_t length;

  (void)context;
  (void)path;
  calls++;
  assert(strcmp(backend->host, "example.test") == 0);
  assert(strcmp(authorization,
                "Bearer accessory_owner_01234567890123456789") == 0);
  if (http_status < 0)
    {
      *response_length = 0u;
      return http_status;
    }

  length = strlen(response_json);
  assert(length < capacity);
  memcpy(response, response_json, length);
  *response_length = length;
  return http_status;
}

static void initialize(lc_accessory_client_t *client,
                       lc_accessory_sync_t *sync)
{
  lc_product_transport_t backend;

  assert(lc_product_transport_init(&backend, "https://example.test/api"));
  assert(lc_accessory_client_init(client, &backend, token, request, NULL));
  assert(lc_accessory_sync_init(sync, client));
}

static void test_poll_routes_independent_checkins_and_presence(void)
{
  lc_accessory_client_t client;
  lc_accessory_sync_t sync;
  lc_accessory_sync_result_t result;

  initialize(&client, &sync);
  response_json =
    "{\"events\":["
    "{\"cursor\":1,\"device_id\":\"cat_a\",\"session_id\":\"a\","
    "\"sequence\":1,\"event\":\"checkin\",\"value\":true},"
    "{\"cursor\":2,\"device_id\":\"mmwave_bridge\","
    "\"session_id\":\"radar\",\"sequence\":1,\"event\":\"presence\","
    "\"value\":true}],\"next_cursor\":2,\"truncated\":false}";
  assert(lc_accessory_sync_poll(&sync, 1000u, &result) ==
         LC_ACCESSORY_CLIENT_OK);
  assert(result.event_count == 2u && result.checkin_changed);
  assert(result.greeting_requested);
  assert((result.actions & LC_ACTION_PLAY_GREETING) != 0u);
  assert(strcmp(result.checkin_status, "A已打卡 · B未打卡") == 0);

  assert((lc_accessory_sync_finish_greeting(&sync, 1100u) &
          LC_ACTION_SHOW_STATE) != 0u);
  response_json =
    "{\"events\":[{\"cursor\":3,\"device_id\":\"cat_b\","
    "\"session_id\":\"b\",\"sequence\":1,\"event\":\"checkin\","
    "\"value\":true}],\"next_cursor\":3,\"truncated\":false}";
  assert(lc_accessory_sync_poll(&sync, 1200u, &result) ==
         LC_ACCESSORY_CLIENT_OK);
  assert(result.checkin_changed && !result.greeting_requested);
  assert(strcmp(result.checkin_status, "A已打卡 · B已打卡") == 0);
}

static void test_network_failure_preserves_state_and_has_no_retry(void)
{
  lc_accessory_client_t client;
  lc_accessory_sync_t sync;
  lc_accessory_sync_result_t result;
  int previous;

  initialize(&client, &sync);
  response_json =
    "{\"events\":[{\"cursor\":1,\"device_id\":\"cat_a\","
    "\"session_id\":\"a\",\"sequence\":1,\"event\":\"checkin\","
    "\"value\":true}],\"next_cursor\":1,\"truncated\":false}";
  assert(lc_accessory_sync_poll(&sync, 1000u, &result) ==
         LC_ACCESSORY_CLIENT_OK);
  previous = calls;
  http_status = -1;
  assert(lc_accessory_sync_poll(&sync, 1100u, &result) ==
         LC_ACCESSORY_CLIENT_NETWORK);
  assert(calls == previous + 1);
  assert(!result.checkin_changed && !result.greeting_requested);
  assert(strcmp(result.checkin_status, "A已打卡 · B未打卡") == 0);
  http_status = 200;
}

static void test_batched_leave_rearms_a_later_greeting(void)
{
  lc_accessory_client_t client;
  lc_accessory_sync_t sync;
  lc_accessory_sync_result_t result;

  initialize(&client, &sync);
  response_json =
    "{\"events\":["
    "{\"cursor\":1,\"device_id\":\"mmwave_bridge\","
    "\"session_id\":\"radar\",\"sequence\":1,\"event\":\"presence\","
    "\"value\":true},"
    "{\"cursor\":2,\"device_id\":\"mmwave_bridge\","
    "\"session_id\":\"radar\",\"sequence\":2,\"event\":\"presence\","
    "\"value\":false}],\"next_cursor\":2,\"truncated\":false}";
  assert(lc_accessory_sync_poll(&sync, 1000u, &result) ==
         LC_ACCESSORY_CLIENT_OK);
  assert(result.greeting_requested);
  assert((lc_accessory_sync_finish_greeting(&sync, 1100u) &
          LC_ACTION_SHOW_STATE) != 0u);

  response_json =
    "{\"events\":[],\"next_cursor\":2,\"truncated\":false}";
  assert(lc_accessory_sync_poll(&sync, 2200u, &result) ==
         LC_ACCESSORY_CLIENT_OK);
  assert(!result.greeting_requested);
  assert(!sync.presence_runtime.presence.present);

  response_json =
    "{\"events\":[{\"cursor\":3,\"device_id\":\"mmwave_bridge\","
    "\"session_id\":\"radar\",\"sequence\":3,\"event\":\"presence\","
    "\"value\":true}],\"next_cursor\":3,\"truncated\":false}";
  assert(lc_accessory_sync_poll(&sync, 32000u, &result) ==
         LC_ACCESSORY_CLIENT_OK);
  assert(result.greeting_requested);
  assert((result.actions & LC_ACTION_PLAY_GREETING) != 0u);
}

static void test_reset_and_truncation_are_explicit(void)
{
  lc_accessory_client_t client;
  lc_accessory_sync_t sync;
  lc_accessory_sync_result_t result;

  initialize(&client, &sync);
  response_json =
    "{\"events\":[{\"cursor\":9,\"device_id\":\"cat_b\","
    "\"session_id\":\"b\",\"sequence\":1,\"event\":\"checkin\","
    "\"value\":true}],\"next_cursor\":9,\"truncated\":true}";
  assert(lc_accessory_sync_poll(&sync, 1000u, &result) ==
         LC_ACCESSORY_CLIENT_OK);
  assert(result.history_truncated && !result.server_reset);

  http_status = 409;
  response_json = "{\"detail\":\"accessory_cursor_ahead\"}";
  assert(lc_accessory_sync_poll(&sync, 1100u, &result) ==
         LC_ACCESSORY_CLIENT_RESET);
  assert(result.server_reset && !result.history_truncated);
  http_status = 200;
}

static void test_invalid_arguments(void)
{
  lc_accessory_sync_t sync;
  lc_accessory_sync_result_t result;

  memset(&sync, 0, sizeof(sync));
  assert(!lc_accessory_sync_init(NULL, NULL));
  assert(!lc_accessory_sync_init(&sync, NULL));
  assert(lc_accessory_sync_poll(NULL, 0u, &result) ==
         LC_ACCESSORY_CLIENT_INVALID);
  assert(lc_accessory_sync_poll(&sync, 0u, &result) ==
         LC_ACCESSORY_CLIENT_INVALID);
  assert(lc_accessory_sync_poll(&sync, 0u, NULL) ==
         LC_ACCESSORY_CLIENT_INVALID);
  assert(lc_accessory_sync_finish_greeting(&sync, 0u) == LC_ACTION_NONE);
}

int main(void)
{
  test_poll_routes_independent_checkins_and_presence();
  test_network_failure_preserves_state_and_has_no_retry();
  test_batched_leave_rearms_a_later_greeting();
  test_reset_and_truncation_are_explicit();
  test_invalid_arguments();
  puts("PASS: authenticated accessory relay coordinator");
  return 0;
}
