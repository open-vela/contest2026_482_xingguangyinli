#include "lc_local_accessory_server.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
  int open_result;
  int receive_result;
  uint8_t receive_packet[LC_LOCAL_ACCESSORY_PACKET_MAX];
  size_t receive_length;
  uint8_t sent_packet[LC_LOCAL_ACCESSORY_PACKET_MAX];
  size_t sent_length;
  unsigned int open_calls;
  unsigned int receive_calls;
  unsigned int send_calls;
  unsigned int close_calls;
} fake_socket_t;

typedef struct
{
  unsigned int calls;
  lc_local_accessory_event_t event;
  bool reject;
} event_sink_t;

static const uint8_t TEST_CAT_A_KEY[LC_LOCAL_ACCESSORY_KEY_BYTES] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};

static const uint8_t TEST_CAT_B_KEY[LC_LOCAL_ACCESSORY_KEY_BYTES] = {
  0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
  0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
  0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f
};

static bool fake_hmac(void *context,
                      const uint8_t key[LC_LOCAL_ACCESSORY_KEY_BYTES],
                      const uint8_t *message, size_t message_length,
                      uint8_t output[LC_LOCAL_ACCESSORY_TAG_BYTES])
{
  uint32_t state = 2166136261u;
  size_t index;

  (void)context;
  for (index = 0u; index < LC_LOCAL_ACCESSORY_KEY_BYTES; index++)
    {
      state = (state ^ key[index]) * 16777619u;
    }
  for (index = 0u; index < message_length; index++)
    {
      state = (state ^ message[index]) * 16777619u;
    }
  for (index = 0u; index < LC_LOCAL_ACCESSORY_TAG_BYTES; index++)
    {
      state ^= state >> 13;
      state *= 0x5bd1e995u;
      output[index] = (uint8_t)(state >> ((index % 4u) * 8u));
    }
  return true;
}

static int fake_open(void *context, uint16_t port, uint32_t timeout_ms)
{
  fake_socket_t *socket = context;
  assert(port == 18765u);
  assert(timeout_ms == 200u);
  socket->open_calls++;
  return socket->open_result;
}

static int fake_receive(void *context, int handle, uint8_t *packet,
                        size_t packet_capacity, uint8_t *peer,
                        size_t *peer_length)
{
  fake_socket_t *socket = context;
  assert(handle == 7);
  socket->receive_calls++;
  if (socket->receive_result <= 0)
    {
      return socket->receive_result;
    }
  assert(socket->receive_length <= packet_capacity);
  assert(*peer_length >= 4u);
  memcpy(packet, socket->receive_packet, socket->receive_length);
  memcpy(peer, "peer", 4u);
  *peer_length = 4u;
  return (int)socket->receive_length;
}

static bool fake_send(void *context, int handle, const uint8_t *packet,
                      size_t packet_length, const uint8_t *peer,
                      size_t peer_length)
{
  fake_socket_t *socket = context;
  assert(handle == 7);
  assert(peer_length == 4u && memcmp(peer, "peer", 4u) == 0);
  assert(packet_length < sizeof(socket->sent_packet));
  memcpy(socket->sent_packet, packet, packet_length);
  socket->sent_length = packet_length;
  socket->send_calls++;
  return true;
}

static void fake_close(void *context, int handle)
{
  fake_socket_t *socket = context;
  assert(handle == 7);
  socket->close_calls++;
}

static bool accept_event(void *context,
                         const lc_local_accessory_event_t *event)
{
  event_sink_t *sink = context;
  sink->calls++;
  sink->event = *event;
  return !sink->reject;
}

static lc_local_accessory_server_config_t config_for(fake_socket_t *socket,
                                                      event_sink_t *sink)
{
  lc_local_accessory_server_config_t config;
  memset(&config, 0, sizeof(config));
  config.port = 18765u;
  config.receive_timeout_ms = 200u;
  memcpy(config.cat_a_key, TEST_CAT_A_KEY, sizeof(config.cat_a_key));
  memcpy(config.cat_b_key, TEST_CAT_B_KEY, sizeof(config.cat_b_key));
  config.hmac = fake_hmac;
  config.on_event = accept_event;
  config.event_context = sink;
  config.socket_context = socket;
  config.socket_ops.open = fake_open;
  config.socket_ops.receive = fake_receive;
  config.socket_ops.send = fake_send;
  config.socket_ops.close = fake_close;
  return config;
}

static void private_key_config_is_strict_and_named(void)
{
  char document[160];
  lc_local_accessory_keys_t keys;
  int written;
  size_t index;

  written = snprintf(document, sizeof(document),
                     "cat_a=%064x\ncat_b=%064x\n", 1, 2);
  assert(written > 0 && (size_t)written < sizeof(document));
  assert(lc_local_accessory_keys_parse(document, (size_t)written, &keys));
  for (index = 0u; index + 1u < LC_LOCAL_ACCESSORY_KEY_BYTES; index++)
    {
      assert(keys.cat_a[index] == 0u);
      assert(keys.cat_b[index] == 0u);
    }
  assert(keys.cat_a[LC_LOCAL_ACCESSORY_KEY_BYTES - 1u] == 1u);
  assert(keys.cat_b[LC_LOCAL_ACCESSORY_KEY_BYTES - 1u] == 2u);

  document[6] = 'A';
  assert(!lc_local_accessory_keys_parse(document, (size_t)written, &keys));
  document[6] = '0';
  document[0] = 'x';
  assert(!lc_local_accessory_keys_parse(document, (size_t)written, &keys));
  document[0] = 'c';
  assert(!lc_local_accessory_keys_parse(document, (size_t)written - 1u,
                                        &keys));
}

static void load_request(fake_socket_t *socket, const char *device,
                         const char *session, uint32_t sequence,
                         const uint8_t key[LC_LOCAL_ACCESSORY_KEY_BYTES])
{
  char request[LC_LOCAL_ACCESSORY_PACKET_MAX];
  assert(lc_local_accessory_request_format(device, session, sequence, key,
                                           fake_hmac, NULL, request,
                                           sizeof(request)));
  socket->receive_length = strlen(request);
  memcpy(socket->receive_packet, request, socket->receive_length);
  socket->receive_result = (int)socket->receive_length;
}

static void bind_failure_and_timeout_are_fail_soft(void)
{
  fake_socket_t socket;
  event_sink_t sink;
  lc_local_accessory_server_t server;
  lc_local_accessory_server_config_t config;

  memset(&socket, 0, sizeof(socket));
  memset(&sink, 0, sizeof(sink));
  socket.open_result = -1;
  config = config_for(&socket, &sink);
  assert(lc_local_accessory_server_init(&server, &config));
  assert(!lc_local_accessory_server_start(&server));
  assert(socket.open_calls == 1u && socket.close_calls == 0u);

  memset(&socket, 0, sizeof(socket));
  socket.open_result = 7;
  config = config_for(&socket, &sink);
  assert(lc_local_accessory_server_init(&server, &config));
  assert(lc_local_accessory_server_start(&server));
  socket.receive_result = LC_LOCAL_ACCESSORY_IO_TIMEOUT;
  assert(lc_local_accessory_server_poll(&server) ==
         LC_LOCAL_ACCESSORY_SERVER_IDLE);
  assert(socket.send_calls == 0u && sink.calls == 0u);
  lc_local_accessory_server_stop(&server);
}

static void accepted_event_and_duplicate_receive_matching_ack(void)
{
  fake_socket_t socket;
  event_sink_t sink;
  lc_local_accessory_server_t server;
  lc_local_accessory_server_config_t config;

  memset(&socket, 0, sizeof(socket));
  memset(&sink, 0, sizeof(sink));
  socket.open_result = 7;
  config = config_for(&socket, &sink);
  assert(lc_local_accessory_server_init(&server, &config));
  assert(lc_local_accessory_server_start(&server));

  load_request(&socket, "cat_a", "boot_A", 1u, TEST_CAT_A_KEY);
  assert(lc_local_accessory_server_poll(&server) ==
         LC_LOCAL_ACCESSORY_SERVER_ACCEPTED);
  assert(sink.calls == 1u);
  assert(strcmp(sink.event.device_id, "cat_a") == 0);
  assert(socket.send_calls == 1u);
  assert(socket.sent_length > strlen("LC2ACK,cat_a,boot_A,1,accepted,"));
  assert(memcmp(socket.sent_packet, "LC2ACK,cat_a,boot_A,1,accepted,",
                strlen("LC2ACK,cat_a,boot_A,1,accepted,")) == 0);

  assert(lc_local_accessory_server_poll(&server) ==
         LC_LOCAL_ACCESSORY_SERVER_DUPLICATE);
  assert(sink.calls == 1u);
  assert(socket.send_calls == 2u);
  assert(socket.sent_length > strlen("LC2ACK,cat_a,boot_A,1,duplicate,"));
  assert(memcmp(socket.sent_packet, "LC2ACK,cat_a,boot_A,1,duplicate,",
                strlen("LC2ACK,cat_a,boot_A,1,duplicate,")) == 0);
  lc_local_accessory_server_stop(&server);
}

static void delivery_failure_does_not_commit_replay_cursor(void)
{
  fake_socket_t socket;
  event_sink_t sink;
  lc_local_accessory_server_t server;
  lc_local_accessory_server_config_t config;

  memset(&socket, 0, sizeof(socket));
  memset(&sink, 0, sizeof(sink));
  socket.open_result = 7;
  sink.reject = true;
  config = config_for(&socket, &sink);
  assert(lc_local_accessory_server_init(&server, &config));
  assert(lc_local_accessory_server_start(&server));

  load_request(&socket, "cat_b", "boot_B-delivery", 4u, TEST_CAT_B_KEY);
  assert(lc_local_accessory_server_poll(&server) ==
         LC_LOCAL_ACCESSORY_SERVER_DELIVERY_FAILED);
  assert(sink.calls == 1u);
  assert(socket.send_calls == 0u);

  sink.reject = false;
  assert(lc_local_accessory_server_poll(&server) ==
         LC_LOCAL_ACCESSORY_SERVER_ACCEPTED);
  assert(sink.calls == 2u);
  assert(socket.send_calls == 1u);
  assert(socket.sent_length >
         strlen("LC2ACK,cat_b,boot_B-delivery,4,accepted,"));
  assert(memcmp(socket.sent_packet,
                "LC2ACK,cat_b,boot_B-delivery,4,accepted,",
                strlen("LC2ACK,cat_b,boot_B-delivery,4,accepted,")) == 0);
  lc_local_accessory_server_stop(&server);
}

static void stop_is_clean_and_idempotent(void)
{
  fake_socket_t socket;
  event_sink_t sink;
  lc_local_accessory_server_t server;
  lc_local_accessory_server_config_t config;

  memset(&socket, 0, sizeof(socket));
  memset(&sink, 0, sizeof(sink));
  socket.open_result = 7;
  config = config_for(&socket, &sink);
  assert(lc_local_accessory_server_init(&server, &config));
  assert(lc_local_accessory_server_start(&server));
  lc_local_accessory_server_request_stop(&server);
  assert(lc_local_accessory_server_poll(&server) ==
         LC_LOCAL_ACCESSORY_SERVER_STOPPED);
  assert(socket.close_calls == 1u);
  lc_local_accessory_server_stop(&server);
  assert(socket.close_calls == 1u);
}

int main(void)
{
  private_key_config_is_strict_and_named();
  bind_failure_and_timeout_are_fail_soft();
  accepted_event_and_duplicate_receive_matching_ack();
  delivery_failure_does_not_commit_replay_cursor();
  stop_is_clean_and_idempotent();
  puts("lc_local_accessory_server tests passed");
  return 0;
}
