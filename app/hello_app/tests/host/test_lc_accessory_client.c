#include "lc_accessory_client.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char token[] = "accessory_owner_01234567890123456789";
static const char *response_json;
static int http_status = 200;
static int calls;
static char last_path[192];

static int request(void *context, const lc_product_transport_t *backend,
                   const char *path, const char *authorization,
                   char *response, size_t capacity, size_t *response_length)
{
  size_t length;

  (void)context;
  calls++;
  assert(strcmp(backend->host, "example.test") == 0);
  assert(strcmp(authorization,
                "Bearer accessory_owner_01234567890123456789") == 0);
  assert(strlen(path) < sizeof(last_path));
  strcpy(last_path, path);
  length = strlen(response_json);
  assert(length < capacity);
  memcpy(response, response_json, length);
  *response_length = length;
  return http_status;
}

static void initialize(lc_accessory_client_t *client)
{
  lc_product_transport_t backend;

  assert(lc_product_transport_init(&backend, "https://example.test/api"));
  assert(lc_accessory_client_init(client, &backend, token, request, NULL));
}

static void test_token_stream_is_single_bounded_secret(void)
{
  lc_product_transport_t backend;
  lc_accessory_client_t client;
  const char *cases[] = {
    token,
    "accessory_owner_01234567890123456789\n",
    "accessory_owner_01234567890123456789\r\n",
    "accessory_owner_01234567890123456789\nsecond",
    "accessory_owner_01234567890123456789\rhidden",
  };
  size_t index;

  assert(lc_product_transport_init(&backend, "https://example.test/api"));
  for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++)
    {
      FILE *stream = tmpfile();
      assert(stream != NULL);
      assert(fputs(cases[index], stream) >= 0);
      rewind(stream);
      assert(lc_accessory_client_init_stream(
               &client, &backend, stream, request, NULL) == (index < 3u));
      fclose(stream);
    }

  assert(!lc_accessory_client_init(&client, &backend, "short", request, NULL));
  assert(!lc_accessory_client_init(
    &client, &backend, "s" "k-test-not-a-real-secret-1234567890", request,
    NULL));
  assert(!lc_product_transport_init(
    &backend, "https://example.test/api\r\nInjected:1"));
  assert(lc_product_transport_init(&backend, "https://example.test/api"));
  strcpy(backend.origin, "https://example.test/api\r\nInjected:1");
  assert(!lc_accessory_client_init(&client, &backend, token, request, NULL));
}

static void test_poll_validates_events_and_advances_server_cursor(void)
{
  lc_accessory_client_t client;
  lc_accessory_event_t events[4];
  size_t count = 99u;
  bool truncated = true;

  initialize(&client);
  response_json =
    "{\"events\":["
    "{\"cursor\":1,\"device_id\":\"cat_a\",\"session_id\":\"boot-a\","
    "\"sequence\":1,\"event\":\"checkin\",\"value\":true},"
    "{\"cursor\":2,\"device_id\":\"mmwave_bridge\","
    "\"session_id\":\"radar-a\",\"sequence\":7,\"event\":\"presence\","
    "\"value\":false}],\"next_cursor\":2,\"truncated\":false}";
  assert(lc_accessory_client_poll(&client, events, 4u, &count, &truncated) ==
         LC_ACCESSORY_CLIENT_OK);
  assert(strcmp(last_path, "/api/v1/accessories/events?after=0&limit=4") == 0);
  assert(count == 2u && !truncated && client.cursor == 2u);
  assert(strcmp(events[0].device_id, "cat_a") == 0);
  assert(events[0].type == LC_ACCESSORY_CHECKIN && events[0].value == 1);
  assert(events[1].type == LC_ACCESSORY_PRESENCE && events[1].value == 0);

  response_json = "{\"events\":[],\"next_cursor\":2,\"truncated\":false}";
  assert(lc_accessory_client_poll(&client, events, 4u, &count, &truncated) ==
         LC_ACCESSORY_CLIENT_OK);
  assert(strcmp(last_path, "/api/v1/accessories/events?after=2&limit=4") == 0);
  assert(count == 0u && client.cursor == 2u);
}

static void test_schema_failure_never_advances_or_publishes_partial_events(void)
{
  lc_accessory_client_t client;
  lc_accessory_event_t events[2];
  size_t count;
  bool truncated;
  const char *invalid[] = {
    "{\"events\":[{\"cursor\":1,\"device_id\":\"cat_c\","
      "\"session_id\":\"x\",\"sequence\":1,\"event\":\"checkin\","
      "\"value\":true}],\"next_cursor\":1,\"truncated\":false}",
    "{\"events\":[{\"cursor\":1,\"device_id\":\"cat_a\","
      "\"session_id\":\"x\",\"sequence\":1,\"event\":\"checkin\","
      "\"value\":1}],\"next_cursor\":1,\"truncated\":false}",
    "{\"events\":[{\"cursor\":1,\"device_id\":\"cat_a\","
      "\"session_id\":\"x\",\"sequence\":1,\"event\":\"presence\","
      "\"value\":true}],\"next_cursor\":1,\"truncated\":false}",
    "{\"events\":[{\"cursor\":2,\"device_id\":\"cat_a\","
      "\"session_id\":\"x\",\"sequence\":1,\"event\":\"checkin\","
      "\"value\":true}],\"next_cursor\":2,\"truncated\":false}",
    ("{\"events\":[],\"next_cursor\":1,\"next_cursor\":1,"
      "\"truncated\":false}"),
    "{\"events\":[],\"next_cursor\":0,\"truncated\":false,\"extra\":1}",
  };
  size_t index;

  initialize(&client);
  for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); index++)
    {
      memset(events, 0xa5, sizeof(events));
      count = 77u;
      truncated = true;
      response_json = invalid[index];
      assert(lc_accessory_client_poll(
               &client, events, 2u, &count, &truncated) ==
             LC_ACCESSORY_CLIENT_SCHEMA);
      assert(count == 0u && !truncated && client.cursor == 0u);
      assert(events[0].device_id[0] == '\0');
    }
}

static void test_history_truncation_and_server_restart_are_explicit(void)
{
  lc_accessory_client_t client;
  lc_accessory_event_t event;
  size_t count;
  bool truncated;

  initialize(&client);
  response_json =
    "{\"events\":[{\"cursor\":9,\"device_id\":\"cat_b\","
    "\"session_id\":\"boot-b\",\"sequence\":3,\"event\":\"checkin\","
    "\"value\":true}],\"next_cursor\":9,\"truncated\":true}";
  assert(lc_accessory_client_poll(&client, &event, 1u, &count, &truncated) ==
         LC_ACCESSORY_CLIENT_OK);
  assert(truncated && count == 1u && client.cursor == 9u);

  http_status = 409;
  response_json = "{\"detail\":\"accessory_cursor_ahead\"}";
  assert(lc_accessory_client_poll(&client, &event, 1u, &count, &truncated) ==
         LC_ACCESSORY_CLIENT_RESET);
  assert(client.cursor == 0u && count == 0u && !truncated);
  http_status = 200;
}

static void test_network_and_http_errors_are_single_attempts(void)
{
  lc_accessory_client_t client;
  lc_accessory_event_t event;
  size_t count;
  bool truncated;
  int previous;

  initialize(&client);
  response_json = "{}";
  previous = calls;
  http_status = -1;
  assert(lc_accessory_client_poll(&client, &event, 1u, &count, &truncated) ==
         LC_ACCESSORY_CLIENT_NETWORK);
  assert(calls == previous + 1);
  http_status = 401;
  assert(lc_accessory_client_poll(&client, &event, 1u, &count, &truncated) ==
         LC_ACCESSORY_CLIENT_HTTP);
  assert(calls == previous + 2);
  http_status = 200;

  lc_accessory_client_clear(&client);
  assert(!client.configured && client.authorization[0] == '\0');
}

int main(void)
{
  test_token_stream_is_single_bounded_secret();
  test_poll_validates_events_and_advances_server_cursor();
  test_schema_failure_never_advances_or_publishes_partial_events();
  test_history_truncation_and_server_restart_are_explicit();
  test_network_and_http_errors_are_single_attempts();
  puts("PASS: authenticated bounded accessory polling client");
  return 0;
}
