#include "lc_private_http.h"
#include "../../src/lc_http_framing.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
  const unsigned char *reply;
  size_t reply_length;
  size_t reply_offset;
  unsigned char sent[4096];
  size_t sent_length;
  unsigned int opened;
  unsigned int closed;
} fake_io_t;

static intptr_t fake_open(void *context, const char *host, unsigned int port)
{
  fake_io_t *io = context;
  assert((strcmp(host, "192.168.8.20") == 0 && port == 8765u) ||
         (strcmp(host, "api.example.test") == 0 && port == 443u));
  io->opened++;
  return 7;
}

static int fake_send(void *context, intptr_t handle,
                     const void *bytes, size_t length)
{
  fake_io_t *io = context;
  size_t chunk = length > 7u ? 7u : length;
  assert(handle == 7);
  assert(io->sent_length + chunk <= sizeof(io->sent));
  memcpy(io->sent + io->sent_length, bytes, chunk);
  io->sent_length += chunk;
  return (int)chunk;
}

static int fake_receive(void *context, intptr_t handle,
                        void *bytes, size_t capacity)
{
  fake_io_t *io = context;
  size_t remaining = io->reply_length - io->reply_offset;
  size_t chunk = remaining > 5u ? 5u : remaining;
  assert(handle == 7);
  if (chunk > capacity) chunk = capacity;
  if (chunk == 0u) return 0;
  memcpy(bytes, io->reply + io->reply_offset, chunk);
  io->reply_offset += chunk;
  return (int)chunk;
}

static void fake_close(void *context, intptr_t handle)
{
  fake_io_t *io = context;
  assert(handle == 7);
  io->closed++;
}

static const lc_private_http_ops_t g_ops = {
  fake_open, fake_send, fake_receive, fake_close
};

static int request(fake_io_t *io, const char *path,
                   const lc_private_http_header_t *headers,
                   const void *body, size_t body_length,
                   unsigned char *response, size_t capacity,
                   size_t *response_length)
{
  return lc_private_http_request_with_ops(
    &g_ops, io, "192.168.8.20", "8765", "POST", path, headers,
    body, body_length, response, capacity, response_length);
}

static int public_request(fake_io_t *io, const char *path,
                          unsigned char *response, size_t capacity,
                          size_t *response_length)
{
  static const lc_private_http_header_t headers[] = {
    {"Content-Type", "application/json"}, {NULL, NULL}
  };
  return lc_http_framed_request_with_ops(
    &g_ops, io, false, "api.example.test", "443", "POST", path, headers,
    "{}", 2u, response, capacity, response_length);
}

static void test_binary_safe_fragmented_round_trip(void)
{
  static const unsigned char reply[] = {
    'H','T','T','P','/','1','.','1',' ','2','0','0',' ','O','K','\r','\n',
    'C','o','n','t','e','n','t','-','L','e','n','g','t','h',':',' ','5','\r','\n',
    'C','o','n','n','e','c','t','i','o','n',':',' ','c','l','o','s','e','\r','\n',
    '\r','\n', 0x52, 0x00, 0x49, 0x46, 0x46
  };
  static const unsigned char body[] = {0x52, 0x49, 0x00, 0x46};
  static const lc_private_http_header_t headers[] = {
    {"Content-Type", "audio/wav"},
    {"Authorization", "Bearer synthetic-owner-device-token-0000"},
    {NULL, NULL}
  };
  fake_io_t io = {reply, sizeof(reply), 0u, {0}, 0u, 0u, 0u};
  unsigned char response[16] = {0xff};
  size_t received = 99u;

  assert(request(&io, "/v1/voice/asr?session_id=meal_0123456789ab",
                 headers, body, sizeof(body), response, sizeof(response),
                 &received) == 200);
  assert(received == 5u);
  assert(memcmp(response, "R\0IFF", 5u) == 0);
  assert(response[5] == 0u);
  assert(io.opened == 1u && io.closed == 1u);
  assert(memmem(io.sent, io.sent_length,
                "Host: 192.168.8.20:8765\r\n",
                strlen("Host: 192.168.8.20:8765\r\n")) != NULL);
  assert(memmem(io.sent, io.sent_length,
                "Content-Length: 4\r\n",
                strlen("Content-Length: 4\r\n")) != NULL);
  assert(memmem(io.sent, io.sent_length, body, sizeof(body)) != NULL);
}

static void test_private_destination_and_request_injection_gate(void)
{
  static const unsigned char reply[] =
    "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\n\r\n";
  static const lc_private_http_header_t good[] = {
    {"Content-Type", "application/json"}, {NULL, NULL}
  };
  static const lc_private_http_header_t injected[] = {
    {"Content-Type", "application/json\r\nX-Evil: 1"}, {NULL, NULL}
  };
  static const lc_private_http_header_t transfer_encoded[] = {
    {"Transfer-Encoding", "chunked"}, {NULL, NULL}
  };
  static const lc_private_http_header_t expect_continue[] = {
    {"Expect", "100-continue"}, {NULL, NULL}
  };
  lc_private_http_header_t unterminated[8];
  size_t index;
  fake_io_t io = {(const unsigned char *)reply, sizeof(reply) - 1u,
                  0u, {0}, 0u, 0u, 0u};
  unsigned char response[2] = {0xff, 0xff};
  size_t received = 9u;

  assert(lc_private_http_request_with_ops(
    &g_ops, &io, "8.8.8.8", "8765", "POST", "/v1/session", good,
    "{}", 2u, response, sizeof(response), &received) ==
    LC_PRIVATE_HTTP_INVALID);
  assert(lc_private_http_request_with_ops(
    &g_ops, &io, "192.168.8.20", "80", "POST", "/v1/session", good,
    "{}", 2u, response, sizeof(response), &received) ==
    LC_PRIVATE_HTTP_INVALID);
  assert(lc_private_http_request_with_ops(
    &g_ops, &io, "192.168.8.20", "8765", "POST", "/ok\r\nX: 1", good,
    "{}", 2u, response, sizeof(response), &received) ==
    LC_PRIVATE_HTTP_INVALID);
  assert(lc_private_http_request_with_ops(
    &g_ops, &io, "192.168.8.20", "8765", "POST", "/v1/session", injected,
    "{}", 2u, response, sizeof(response), &received) ==
    LC_PRIVATE_HTTP_INVALID);
  assert(lc_private_http_request_with_ops(
    &g_ops, &io, "192.168.8.20", "8765", "POST", "/v1/session",
    transfer_encoded, "{}", 2u, response, sizeof(response), &received) ==
    LC_PRIVATE_HTTP_INVALID);
  assert(lc_private_http_request_with_ops(
    &g_ops, &io, "192.168.8.20", "8765", "POST", "/v1/session",
    expect_continue, "{}", 2u, response, sizeof(response), &received) ==
    LC_PRIVATE_HTTP_INVALID);
  for (index = 0u; index < 8u; index++)
    {
      unterminated[index].name = "X-Test";
      unterminated[index].value = "safe";
    }
  assert(lc_private_http_request_with_ops(
    &g_ops, &io, "192.168.8.20", "8765", "POST", "/v1/session",
    unterminated, "{}", 2u, response, sizeof(response), &received) ==
    LC_PRIVATE_HTTP_INVALID);
  assert(io.opened == 0u && io.closed == 0u);
  assert(received == 0u);
}

static void expect_bad_reply(const char *reply, size_t response_capacity,
                             int expected)
{
  static const lc_private_http_header_t headers[] = {
    {"Content-Type", "application/json"}, {NULL, NULL}
  };
  fake_io_t io = {(const unsigned char *)reply, strlen(reply),
                  0u, {0}, 0u, 0u, 0u};
  unsigned char response[16] = {0};
  size_t received = 8u;
  assert(request(&io, "/v1/session", headers, "{}", 2u,
                 response, response_capacity, &received) == expected);
  assert(io.opened == 1u && io.closed == 1u);
  assert(received == 0u);
}

static void test_response_framing_is_strict(void)
{
  expect_bad_reply("HTTP/1.1 200 OK\r\n\r\n{}", 16u,
                   LC_PRIVATE_HTTP_PROTOCOL);
  expect_bad_reply("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n"
                   "Content-Length: 2\r\n\r\n{}", 16u,
                   LC_PRIVATE_HTTP_PROTOCOL);
  expect_bad_reply("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                   "2\r\n{}\r\n0\r\n\r\n", 16u,
                   LC_PRIVATE_HTTP_PROTOCOL);
  expect_bad_reply("HTTP/1.1 200 OK\r\nTransfer-Encoding : chunked\r\n"
                   "Content-Length: 2\r\n\r\n{}", 16u,
                   LC_PRIVATE_HTTP_PROTOCOL);
  expect_bad_reply("HTTP/1.1 200 OK\r\nTrailer : Digest\r\n"
                   "Content-Length: 2\r\n\r\n{}", 16u,
                   LC_PRIVATE_HTTP_PROTOCOL);
  expect_bad_reply("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\n{}", 16u,
                   LC_PRIVATE_HTTP_READ);
  expect_bad_reply("HTTP/1.1 200 OK\r\nContent-Length: 16\r\n\r\n"
                   "0123456789abcdef", 16u, LC_PRIVATE_HTTP_OVERFLOW);
}

static void test_public_transport_uses_the_same_strict_framing(void)
{
  static const unsigned char good[] =
    "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}";
  static const unsigned char missing[] =
    "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n{}";
  unsigned char response[16] = {0};
  size_t received = 0u;
  fake_io_t io = {good, sizeof(good) - 1u, 0u, {0}, 0u, 0u, 0u};

  assert(public_request(&io, "/v1/session", response, sizeof(response),
                        &received) == 200);
  assert(received == 2u && memcmp(response, "{}", 2u) == 0);

  io = (fake_io_t){missing, sizeof(missing) - 1u, 0u, {0}, 0u, 0u, 0u};
  assert(public_request(&io, "/v1/session", response, sizeof(response),
                        &received) == LC_PRIVATE_HTTP_PROTOCOL);
  assert(received == 0u);
}

static void test_verified_public_get_is_narrowly_supported(void)
{
  static const unsigned char reply[] =
    "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n[]";
  static const lc_private_http_header_t headers[] = {
    {"Accept", "application/json"}, {NULL, NULL}
  };
  fake_io_t io = {reply, sizeof(reply) - 1u, 0u, {0}, 0u, 0u, 0u};
  unsigned char response[8] = {0};
  size_t received = 0u;

  assert(lc_http_framed_request_with_ops(
    &g_ops, &io, false, "api.example.test", "443", "GET", "/v1/events",
    headers, NULL, 0u, response, sizeof(response), &received) == 200);
  assert(received == 2u && memcmp(response, "[]", 2u) == 0);

  io = (fake_io_t){reply, sizeof(reply) - 1u, 0u, {0}, 0u, 0u, 0u};
  assert(lc_http_framed_request_with_ops(
    &g_ops, &io, true, "192.168.8.20", "8765", "GET", "/v1/events",
    headers, NULL, 0u, response, sizeof(response), &received) ==
    LC_PRIVATE_HTTP_INVALID);
  assert(io.opened == 0u);
}

int main(void)
{
  test_binary_safe_fragmented_round_trip();
  test_private_destination_and_request_injection_gate();
  test_response_framing_is_strict();
  test_public_transport_uses_the_same_strict_framing();
  test_verified_public_get_is_narrowly_supported();
  puts("PASS: strict private-LAN binary HTTP transport");
  return 0;
}
