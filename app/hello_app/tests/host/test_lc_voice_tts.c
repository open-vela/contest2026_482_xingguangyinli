#include "lc_voice_client.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char session[] = "meal_0123456789ab";
static const char phrase[] = "想试试照烧鸡腿饭吗？";
static uint8_t reply[46] = {
  'R','I','F','F',38,0,0,0,'W','A','V','E','f','m','t',' ',
  16,0,0,0,1,0,1,0,0xc0,0x5d,0,0,0x80,0xbb,0,0,2,0,16,0,
  'd','a','t','a',2,0,0,0,0x21,0
};
static int calls;
static int http_status = 200;
static bool current = true;
static bool cancel_during_request;
static bool truncated_response;
static bool maximum_response;
static const char *expected_text = phrase;

static bool is_current(void *context, const char *id)
{
  (void)context;
  assert(strcmp(id, session) == 0);
  return current;
}

static void le32(uint8_t *p, uint32_t value)
{
  for (size_t i = 0; i < 4u; i++) p[i] = (uint8_t)(value >> (8u * i));
}

static int request(void *context, const lc_product_transport_t *backend,
  const char *path, const char *mime, const char *authorization,
  const void *body, size_t body_length, char *response, size_t capacity,
  size_t *received)
{
  (void)context;
  calls++;
  assert(strcmp(backend->host, "example.test") == 0);
  assert(strcmp(path, "/api/v1/voice/tts?session_id=meal_0123456789ab") == 0);
  assert(strcmp(mime, "text/plain; charset=utf-8") == 0);
  assert(strcmp(authorization, "Bearer lc-owner-test-device-01234567890123456789") == 0);
  assert(body_length == strlen(expected_text));
  assert(memcmp(body, expected_text, body_length) == 0);
  assert(capacity == LC_VOICE_SYNTHESIS_MAX + 2u);
  if (cancel_during_request) current = false;
  memcpy(response, reply, sizeof(reply)); /* PCM contains NUL: explicit length. */
  *received = sizeof(reply);
  if (maximum_response)
    {
      memset(response + sizeof(reply), 0, LC_VOICE_SYNTHESIS_MAX - sizeof(reply));
      le32((uint8_t *)response + 4, LC_VOICE_SYNTHESIS_MAX - 8u);
      le32((uint8_t *)response + 40, LC_VOICE_SYNTHESIS_MAX - 44u);
      *received = LC_VOICE_SYNTHESIS_MAX;
    }
  if (truncated_response) *received = capacity - 1u;
  return http_status;
}

static void expect_erased(const uint8_t *wav, size_t length)
{
  for (size_t i = 0; i < length; i++) assert(wav[i] == 0u);
}

int main(void)
{
  lc_product_transport_t backend;
  lc_voice_client_t client;
  assert(lc_product_transport_init(&backend, "https://example.test/api"));
  assert(lc_voice_client_init(&client, &backend,
    "lc-owner-test-device-01234567890123456789", "gemini-s1", request, NULL));
  size_t capacity = LC_VOICE_SYNTHESIS_MAX + 2u;
  uint8_t *wav = malloc(capacity);
  assert(wav != NULL);
  size_t length = 999u;
  assert(lc_voice_client_synthesize(&client, session, phrase, wav, capacity,
    &length, is_current, NULL) == LC_VOICE_CLIENT_OK);
  assert(length == sizeof(reply) && memcmp(wav, reply, length) == 0 && calls == 1);

  int previous = calls;
  current = false;
  assert(lc_voice_client_synthesize(&client, session, phrase, wav, capacity,
    &length, is_current, NULL) == LC_VOICE_CLIENT_CANCELLED);
  assert(calls == previous && length == 0u);
  expect_erased(wav, capacity);
  current = true;
  cancel_during_request = true;
  assert(lc_voice_client_synthesize(&client, session, phrase, wav, capacity,
    &length, is_current, NULL) == LC_VOICE_CLIENT_CANCELLED);
  assert(calls == previous + 1 && length == 0u);
  expect_erased(wav, capacity);
  current = true;
  cancel_during_request = false;

  /* Character count, not byte count: 72 Chinese characters are accepted. */
  char chinese[220];
  for (size_t i = 0; i < 73u; i++) memcpy(chinese + i * 3u, "饭", 3u);
  chinese[216] = '\0';
  expected_text = chinese;
  assert(lc_voice_client_synthesize(&client, session, chinese, wav, capacity,
    &length, is_current, NULL) == LC_VOICE_CLIENT_OK);
  memcpy(chinese + 216u, "饭", 4u);
  previous = calls;
  const char *invalid[] = {chinese, "", "   ", "\xc0\xaf", "hello\nworld"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
    {
      assert(lc_voice_client_synthesize(&client, session, invalid[i], wav, capacity,
        &length, is_current, NULL) == LC_VOICE_CLIENT_INVALID);
      assert(length == 0u);
    }
  assert(lc_voice_client_synthesize(&client, "meal_bad&id=x", phrase, wav, capacity,
    &length, is_current, NULL) == LC_VOICE_CLIENT_INVALID);
  assert(lc_voice_client_synthesize(&client, session, phrase, wav, 46u,
    &length, is_current, NULL) == LC_VOICE_CLIENT_INVALID);
  assert(calls == previous);
  expected_text = phrase;

  /* Reject malformed/rate/channel/truncated PCM before any speaker sees it. */
  const size_t bad_offsets[] = {0u, 4u, 8u, 16u, 20u, 22u, 24u, 28u, 32u, 34u, 36u, 40u};
  for (size_t i = 0; i < sizeof(bad_offsets) / sizeof(bad_offsets[0]); i++)
    {
      reply[bad_offsets[i]] ^= 1u;
      assert(lc_voice_client_synthesize(&client, session, phrase, wav, capacity,
        &length, is_current, NULL) == LC_VOICE_CLIENT_SCHEMA);
      assert(length == 0u);
      expect_erased(wav, capacity);
      reply[bad_offsets[i]] ^= 1u;
    }
  truncated_response = true;
  assert(lc_voice_client_synthesize(&client, session, phrase, wav, capacity,
    &length, is_current, NULL) == LC_VOICE_CLIENT_SCHEMA);
  truncated_response = false;
  maximum_response = true;
  assert(lc_voice_client_synthesize(&client, session, phrase, wav, capacity,
    &length, is_current, NULL) == LC_VOICE_CLIENT_OK);
  assert(length == LC_VOICE_SYNTHESIS_MAX); /* Exactly 15 seconds is allowed. */
  maximum_response = false;

  const int failures[] = {302, 401, 429, 503, -1};
  for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); i++)
    {
      http_status = failures[i];
      previous = calls;
      assert(lc_voice_client_synthesize(&client, session, phrase, wav, capacity,
        &length, is_current, NULL) == (http_status < 0 ?
          LC_VOICE_CLIENT_NETWORK : LC_VOICE_CLIENT_HTTP));
      assert(calls == previous + 1 && length == 0u); /* No retries/redirects. */
      expect_erased(wav, capacity);
    }
  lc_voice_client_clear(&client);
  free(wav);
  puts("PASS: TTS Chinese text/binary PCM/caps/stale cancellation/no retries");
  return 0;
}
