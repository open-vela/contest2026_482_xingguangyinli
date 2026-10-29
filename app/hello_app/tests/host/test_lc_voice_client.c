#include "lc_voice_client.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int calls;
static int http_status = 200;
static const char *response_json;
static bool current = true;
static bool cancel_during_request;
static bool oversized_response;
static const char token[] = "lc-owner-test-device-01234567890123456789";
static const char session[] = "meal_0123456789ab";
static uint8_t capture[46] = {
  'R','I','F','F',38,0,0,0,'W','A','V','E','f','m','t',' ',
  16,0,0,0,1,0,1,0,0x80,0x3e,0,0,0,0x7d,0,0,2,0,16,0,
  'd','a','t','a',2,0,0,0,0x21,0
};

static bool is_current(void *context, const char *id)
{
  (void)context;
  assert(strcmp(id, session) == 0);
  return current;
}

static int request(void *context, const lc_product_transport_t *backend,
                   const char *path, const char *mime, const char *authorization,
                   const void *body, size_t body_length,
                   char *response, size_t capacity, size_t *response_length)
{
  (void)context;
  calls++;
  assert(strcmp(backend->host, "example.test") == 0);
  assert(strcmp(authorization, "Bearer lc-owner-test-device-01234567890123456789") == 0);
  if (strcmp(path, "/api/v1/session") == 0)
    {
      assert(strcmp(mime, "application/json") == 0);
      assert(body_length == strlen(body));
      assert(strstr(body, "\"device_id\":\"gemini-s1\"") != NULL);
    }
  else
    {
      assert(strcmp(path, "/api/v1/voice/asr?session_id=meal_0123456789ab") == 0);
      assert(strcmp(mime, "audio/wav") == 0);
      assert(body_length == sizeof(capture)); /* Contains NUL, never strlen. */
      assert(memcmp(body, capture, sizeof(capture)) == 0);
    }
  if (cancel_during_request) current = false;
  size_t length = strlen(response_json);
  assert(length < capacity);
  memcpy(response, response_json, length + 1u);
  *response_length = oversized_response ? capacity - 1u : length;
  return http_status;
}

int main(void)
{
  lc_product_transport_t backend;
  lc_voice_client_t client;
  char id[32];
  char text[2001];
  assert(lc_product_transport_init(&backend, "https://example.test/api"));
  const char *file_cases[] = {token, "lc-owner-test-device-01234567890123456789\n",
    "lc-owner-test-device-01234567890123456789\r\n",
    "lc-owner-test-device-01234567890123456789\rhidden",
    "lc-owner-test-device-01234567890123456789\nsecond-line"};
  for (size_t i = 0; i < sizeof(file_cases) / sizeof(file_cases[0]); i++)
    {
      FILE *file = tmpfile();
      assert(file != NULL);
      assert(fputs(file_cases[i], file) >= 0);
      rewind(file);
      assert(lc_voice_client_init_stream(&client, &backend, file,
        "gemini-s1", request, NULL) == (i < 3u));
      fclose(file);
    }
  FILE *hidden_file = tmpfile();
  assert(hidden_file != NULL);
  assert(fwrite(token, 1u, sizeof(token), hidden_file) == sizeof(token));
  assert(fputs("hidden", hidden_file) >= 0);
  rewind(hidden_file);
  assert(!lc_voice_client_init_stream(&client, &backend, hidden_file,
    "gemini-s1", request, NULL));
  fclose(hidden_file);
  assert(!lc_voice_client_init(&client, &backend, "too-short", "gemini-s1", request, NULL));
  assert(!lc_voice_client_init(&client, &backend,
    "s" "k-test-not-a-real-secret-1234567890", "gemini-s1", request,
    NULL));
  assert(!lc_voice_client_init(&client, &backend, "lc-owner-test-device-01234567890123456789\r\n", "gemini-s1", request, NULL));
  assert(!lc_product_transport_init(&backend,
                                    "https://example.test/api\r\nInjected:1"));
  assert(lc_product_transport_init(&backend, "https://example.test/api"));
  strcpy(backend.origin, "https://example.test/api\r\nInjected:1");
  assert(!lc_voice_client_init(&client, &backend, token, "gemini-s1", request, NULL));
  assert(lc_product_transport_init(&backend, "https://example.test/api"));
  assert(lc_voice_client_init(&client, &backend, token, "gemini-s1", request, NULL));
  response_json = "{\"session_id\":\"meal_0123456789ab\",\"state\":\"listening\"}";
  http_status = 201; /* Actual FastAPI /v1/session status. */
  assert(lc_voice_client_open_session(&client, id, sizeof(id)) == LC_VOICE_CLIENT_OK);
  http_status = 200;
  assert(strcmp(id, session) == 0 && calls == 1);
  response_json = "{\"session_id\":\"meal_0123456789ab\",\"transcript\":\"我想吃面，预算三十元。\"}";
  assert(lc_voice_client_transcribe(&client, id, capture, sizeof(capture), text,
    sizeof(text), is_current, NULL) == LC_VOICE_CLIENT_OK);
  assert(strcmp(text, "我想吃面，预算三十元。") == 0 && calls == 2);

  int previous = calls;
  current = false;
  strcpy(text, "stale");
  assert(lc_voice_client_transcribe(&client, id, capture, sizeof(capture), text,
    sizeof(text), is_current, NULL) == LC_VOICE_CLIENT_CANCELLED);
  assert(calls == previous && text[0] == '\0');
  current = true;
  cancel_during_request = true;
  assert(lc_voice_client_transcribe(&client, id, capture, sizeof(capture), text,
    sizeof(text), is_current, NULL) == LC_VOICE_CLIENT_CANCELLED);
  assert(text[0] == '\0');
  cancel_during_request = false;
  current = true;
  char nested[512];
  strcpy(nested, "{\"extra\":");
  size_t pos = strlen(nested);
  for (int i = 0; i < 32; i++) nested[pos++] = '[';
  nested[pos++] = '0';
  for (int i = 0; i < 32; i++) nested[pos++] = ']';
  strcpy(nested + pos, ",\"session_id\":\"meal_0123456789ab\",\"transcript\":\"hello\"}");
  response_json = nested;
  assert(lc_voice_client_transcribe(&client, id, capture, sizeof(capture), text,
    sizeof(text), is_current, NULL) == LC_VOICE_CLIENT_SCHEMA);
  assert(text[0] == '\0');
  previous = calls;
  assert(lc_voice_client_transcribe(&client, "meal_bad&id=x", capture, sizeof(capture), text,
    sizeof(text), is_current, NULL) == LC_VOICE_CLIENT_INVALID);
  capture[22] = 2;
  assert(lc_voice_client_transcribe(&client, id, capture, sizeof(capture), text,
    sizeof(text), is_current, NULL) == LC_VOICE_CLIENT_INVALID);
  capture[22] = 1;
  assert(lc_voice_client_transcribe(&client, id, capture, 44, text,
    sizeof(text), is_current, NULL) == LC_VOICE_CLIENT_INVALID);
  assert(lc_voice_client_transcribe(&client, id, capture, 480045, text,
    sizeof(text), is_current, NULL) == LC_VOICE_CLIENT_INVALID);
  assert(calls == previous); /* Reject before accessing oversized body. */

  const char *bad[] = {
    "{\"session_id\":\"meal_ffffffffffff\",\"transcript\":\"hello\"}",
    "{\"session_id\":\"meal_0123456789ab\",\"transcript\":\" \"}",
    "{\"session_id\":\"meal_0123456789ab\",\"transcript\":42}",
    "{\"session_id\":\"meal_0123456789ab\",\"transcript\":\"hello\"}junk",
    "{\"session_id\":\"meal_0123456789ab\",\"transcript\":\"a\\u0000b\"}",
    "{\"session_id\":\"meal_0123456789ab\",\"transcript\":\"a\",\"transcript\":\"b\"}",
    "{\"session_id\":\"meal_0123456789ab\",\"transcript\":\"\xc0\xaf\"}"
  };
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
      response_json = bad[i];
      assert(lc_voice_client_transcribe(&client, id, capture, sizeof(capture), text,
        sizeof(text), is_current, NULL) == LC_VOICE_CLIENT_SCHEMA);
      assert(text[0] == '\0');
    }
  response_json = "{\"session_id\":\"meal_0123456789ab\",\"transcript\":\"我想吃面\"}";
  assert(lc_voice_client_transcribe(&client, id, capture, sizeof(capture), text,
    4, is_current, NULL) == LC_VOICE_CLIENT_SCHEMA);
  oversized_response = true;
  assert(lc_voice_client_transcribe(&client, id, capture, sizeof(capture), text,
    sizeof(text), is_current, NULL) == LC_VOICE_CLIENT_SCHEMA);
  oversized_response = false;
  http_status = 401;
  previous = calls;
  assert(lc_voice_client_transcribe(&client, id, capture, sizeof(capture), text,
    sizeof(text), is_current, NULL) == LC_VOICE_CLIENT_HTTP);
  assert(calls == previous + 1); /* No retries on billed calls. */
  http_status = -1;
  assert(lc_voice_client_transcribe(&client, id, capture, sizeof(capture), text,
    sizeof(text), is_current, NULL) == LC_VOICE_CLIENT_NETWORK);
  lc_voice_client_clear(&client);
  assert(!client.configured && client.authorization[0] == '\0');
  puts("PASS: device ASR client/session/auth/binary WAV/UTF8/stale response/error caps");
  return 0;
}
