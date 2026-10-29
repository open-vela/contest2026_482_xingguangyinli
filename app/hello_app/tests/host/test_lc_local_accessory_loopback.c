#include "lc_local_accessory_server.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct
{
  unsigned int cat_a;
  unsigned int cat_b;
} event_counts_t;

static const uint8_t CAT_A_KEY[LC_LOCAL_ACCESSORY_KEY_BYTES] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};

static const uint8_t CAT_B_KEY[LC_LOCAL_ACCESSORY_KEY_BYTES] = {
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

static bool count_event(void *context,
                        const lc_local_accessory_event_t *event)
{
  event_counts_t *counts = context;
  if (event->device == LC_LOCAL_ACCESSORY_CAT_A)
    {
      counts->cat_a++;
      return true;
    }
  if (event->device == LC_LOCAL_ACCESSORY_CAT_B)
    {
      counts->cat_b++;
      return true;
    }
  return false;
}

static bool unused_udp_port(uint16_t *port)
{
  struct sockaddr_in address;
  socklen_t length = sizeof(address);
  int handle = socket(AF_INET, SOCK_DGRAM, 0);

  if (handle < 0 && (errno == EPERM || errno == EACCES))
    {
      return false;
    }
  assert(handle >= 0 && port != NULL);
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (bind(handle, (const struct sockaddr *)&address,
           sizeof(address)) != 0)
    {
      const int failure = errno;
      assert(close(handle) == 0);
      if (failure == EPERM || failure == EACCES)
        {
          return false;
        }
      assert(false);
    }
  assert(getsockname(handle, (struct sockaddr *)&address, &length) == 0);
  assert(close(handle) == 0);
  assert(ntohs(address.sin_port) >= 1024u);
  *port = ntohs(address.sin_port);
  return true;
}

static int open_client(struct sockaddr_in *server_address, uint16_t port)
{
  struct timeval timeout;
  int handle = socket(AF_INET, SOCK_DGRAM, 0);

  assert(handle >= 0);
  timeout.tv_sec = 0;
  timeout.tv_usec = 100000;
  assert(setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                    sizeof(timeout)) == 0);
  memset(server_address, 0, sizeof(*server_address));
  server_address->sin_family = AF_INET;
  server_address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  server_address->sin_port = htons(port);
  return handle;
}

static void send_packet(int client, const struct sockaddr_in *address,
                        const char *packet)
{
  const size_t length = strlen(packet);
  const ssize_t sent = sendto(client, packet, length, 0,
                              (const struct sockaddr *)address,
                              sizeof(*address));
  assert(sent >= 0 && (size_t)sent == length);
}

static void expect_ack(int client, const char *prefix)
{
  char acknowledgement[LC_LOCAL_ACCESSORY_PACKET_MAX];
  const ssize_t received = recv(client, acknowledgement,
                                sizeof(acknowledgement), 0);
  assert(received > 0);
  assert((size_t)received > strlen(prefix));
  assert(memcmp(acknowledgement, prefix, strlen(prefix)) == 0);
}

static void run_20_events(lc_local_accessory_server_t *server,
                          int client,
                          const struct sockaddr_in *address,
                          const char *device,
                          const char *session,
                          const uint8_t key[LC_LOCAL_ACCESSORY_KEY_BYTES])
{
  char packet[LC_LOCAL_ACCESSORY_PACKET_MAX];
  char prefix[96];
  uint32_t sequence;

  for (sequence = 1u; sequence <= 20u; sequence++)
    {
      assert(lc_local_accessory_request_format(
        device, session, sequence, key, fake_hmac, NULL,
        packet, sizeof(packet)));
      send_packet(client, address, packet);
      assert(lc_local_accessory_server_poll(server) ==
             LC_LOCAL_ACCESSORY_SERVER_ACCEPTED);
      assert(snprintf(prefix, sizeof(prefix), "LC2ACK,%s,%s,%lu,accepted,",
                      device, session, (unsigned long)sequence) > 0);
      expect_ack(client, prefix);
    }

  send_packet(client, address, packet);
  assert(lc_local_accessory_server_poll(server) ==
         LC_LOCAL_ACCESSORY_SERVER_DUPLICATE);
  assert(snprintf(prefix, sizeof(prefix), "LC2ACK,%s,%s,20,duplicate,",
                  device, session) > 0);
  expect_ack(client, prefix);
}

int main(void)
{
  event_counts_t counts;
  lc_local_accessory_server_config_t config;
  lc_local_accessory_server_t server;
  struct sockaddr_in server_address;
  char invalid[LC_LOCAL_ACCESSORY_PACKET_MAX];
  char unexpected[LC_LOCAL_ACCESSORY_PACKET_MAX];
  uint16_t port;
  int client;
  ssize_t received;

  memset(&counts, 0, sizeof(counts));
  memset(&config, 0, sizeof(config));
  if (!unused_udp_port(&port))
    {
      puts("SKIP: local UDP bind is blocked by the test sandbox");
      return 0;
    }
  config.port = port;
  config.receive_timeout_ms = 100u;
  memcpy(config.cat_a_key, CAT_A_KEY, sizeof(config.cat_a_key));
  memcpy(config.cat_b_key, CAT_B_KEY, sizeof(config.cat_b_key));
  config.hmac = fake_hmac;
  config.on_event = count_event;
  config.event_context = &counts;
  assert(lc_local_accessory_server_init(&server, &config));
  assert(lc_local_accessory_server_start(&server));
  client = open_client(&server_address, port);

  run_20_events(&server, client, &server_address, "cat_a", "boot_A-loop",
                CAT_A_KEY);
  run_20_events(&server, client, &server_address, "cat_b", "boot_B-loop",
                CAT_B_KEY);
  assert(counts.cat_a == 20u && counts.cat_b == 20u);

  assert(lc_local_accessory_request_format(
    "cat_a", "boot_A-loop", 21u, CAT_A_KEY, fake_hmac, NULL,
    invalid, sizeof(invalid)));
  invalid[strlen(invalid) - 1u] =
    invalid[strlen(invalid) - 1u] == '0' ? '1' : '0';
  send_packet(client, &server_address, invalid);
  assert(lc_local_accessory_server_poll(&server) ==
         LC_LOCAL_ACCESSORY_SERVER_REJECTED);
  received = recv(client, unexpected, sizeof(unexpected), 0);
  assert(received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
  assert(counts.cat_a == 20u && counts.cat_b == 20u);

  assert(close(client) == 0);
  lc_local_accessory_server_stop(&server);
  puts("PASS: real UDP loopback 20/20 per cat, duplicate ACK, tamper no ACK");
  return 0;
}
