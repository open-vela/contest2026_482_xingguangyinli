#include "lc_voice_client.h"
#include "lc_private_http.h"
#include "lc_strict_https.h"
#include "lc_product_trust.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __NuttX__
#include <netutils/cJSON.h>
#else
#include "cJSON.h" /* Actual SDK cJSON for optional host integration test. */
#endif

#define RESPONSE_BYTES 4096u

static void wipe(void *data, size_t length)
{
  volatile unsigned char *bytes = data;
  while (length-- != 0u) *bytes++ = 0u;
}

static bool identifier(const char *value, size_t minimum, size_t maximum)
{
  if (value == NULL) return false;
  size_t length = 0;
  while (value[length] != '\0' && length <= maximum)
    {
      unsigned char c = (unsigned char)value[length++];
      if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
        return false;
    }
  return length >= minimum && length <= maximum;
}

static bool valid_session(const char *id)
{
  if (!identifier(id, 17u, 17u) || strncmp(id, "meal_", 5u) != 0)
    return false;
  for (size_t i = 5u; i < 17u; i++)
    if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f')))
      return false;
  return true;
}

bool lc_voice_client_init_stream(lc_voice_client_t *client,
  const lc_product_transport_t *backend, FILE *stream,
  const char *device_id, lc_voice_request_fn request, void *context)
{
  lc_voice_client_clear(client);
  if (client == NULL || stream == NULL) return false;
  char token[129] = {0};
  size_t length = 0u;
  int c;
  bool complete = false;
  while ((c = fgetc(stream)) != EOF)
    {
      if (c == '\n')
        {
          complete = fgetc(stream) == EOF;
          break;
        }
      if (c == '\r')
        {
          complete = fgetc(stream) == '\n' && fgetc(stream) == EOF;
          break;
        }
      if (c == 0 || length == sizeof(token) - 1u) break;
      token[length++] = (char)c;
    }
  if (c == EOF) complete = true;
  bool valid = complete && !ferror(stream) && lc_voice_client_init(client,
    backend, token, device_id, request, context);
  wipe(token, sizeof(token));
  return valid;
}

void lc_voice_client_clear(lc_voice_client_t *client)
{
  if (client != NULL) wipe(client, sizeof(*client));
}

bool lc_voice_client_init(lc_voice_client_t *client,
  const lc_product_transport_t *backend, const char *device_token,
  const char *device_id, lc_voice_request_fn request, void *context)
{
  if (client == NULL) return false;
  lc_voice_client_clear(client);
  if (backend == NULL || !backend->configured || request == NULL ||
      !lc_product_device_token_valid(device_token) ||
      !identifier(device_id, 1u, sizeof(client->device_id) - 1u)) return false;
  /* Existing URL parser does not reject controls in base_path. Never let
   * runtime configuration inject a request line/header. */
  for (size_t i = 0u; i < sizeof(backend->origin); i++)
    {
      unsigned char c = (unsigned char)backend->origin[i];
      if (c == 0u) break;
      if (c <= 0x20u || c >= 0x7fu) return false;
      if (i == sizeof(backend->origin) - 1u) return false;
    }
  client->backend = *backend;
  (void)snprintf(client->authorization, sizeof(client->authorization),
                 "Bearer %s", device_token);
  memcpy(client->device_id, device_id, strlen(device_id) + 1u);
  client->request = request;
  client->context = context;
  client->configured = true;
  return true;
}

static cJSON *unique_field(cJSON *root, const char *name)
{
  cJSON *found = NULL;
  for (cJSON *item = root->child; item != NULL; item = item->next)
    if (item->string != NULL && strcmp(item->string, name) == 0)
      {
        if (found != NULL) return NULL;
        found = item;
      }
  return found;
}

/* Bound recursive parser stack usage, ignoring brackets inside strings. */
static bool bounded_json(const char *bytes, size_t length)
{
  unsigned int depth = 0u;
  bool quoted = false;
  bool escaped = false;
  for (size_t i = 0u; i < length; i++)
    {
      char c = bytes[i];
      if (quoted)
        {
          if (escaped) escaped = false;
          else if (c == '\\') escaped = true;
          else if (c == '"') quoted = false;
        }
      else if (c == '"') quoted = true;
      else if (c == '{' || c == '[')
        {
          if (++depth > 16u) return false;
        }
      else if (c == '}' || c == ']')
        {
          if (depth == 0u) return false;
          depth--;
        }
    }
  return depth == 0u && !quoted;
}

static lc_voice_client_status_t post(lc_voice_client_t *client,
  const char *endpoint, const char *mime, const void *body, size_t length,
  cJSON **root)
{
  char path[LC_PRODUCT_BASE_PATH_MAX + 96u];
  *root = NULL;
  int n = snprintf(path, sizeof(path), "%s%s", client->backend.base_path, endpoint);
  if (n < 1 || (size_t)n >= sizeof(path)) return LC_VOICE_CLIENT_INVALID;
  char *response = calloc(1u, RESPONSE_BYTES);
  if (response == NULL) return LC_VOICE_CLIENT_NETWORK;
  size_t received = 0u;
  int http = client->request(client->context, &client->backend, path, mime,
    client->authorization, body, length, response, RESPONSE_BYTES, &received);
  lc_voice_client_status_t status = LC_VOICE_CLIENT_SCHEMA;
  if (http < 0) status = LC_VOICE_CLIENT_NETWORK;
  else if (http < 200 || http >= 300) status = LC_VOICE_CLIENT_HTTP;
  else if (received > 0u && received < RESPONSE_BYTES - 1u &&
           memchr(response, '\0', received) == NULL)
    {
      response[received] = '\0';
      if (strstr(response, "\\u0000") == NULL && bounded_json(response, received))
        {
          const char *end = NULL;
          *root = cJSON_ParseWithOpts(response, &end, true);
          if (*root != NULL && cJSON_IsObject(*root)) status = LC_VOICE_CLIENT_OK;
        }
    }
  wipe(response, RESPONSE_BYTES);
  free(response);
  if (status != LC_VOICE_CLIENT_OK)
    {
      cJSON_Delete(*root);
      *root = NULL;
    }
  return status;
}

lc_voice_client_status_t lc_voice_client_open_session(lc_voice_client_t *client,
  char *session_id, size_t capacity)
{
  if (session_id == NULL || capacity == 0u) return LC_VOICE_CLIENT_INVALID;
  session_id[0] = '\0';
  if (client == NULL || !client->configured || capacity < 18u)
    return LC_VOICE_CLIENT_INVALID;
  char body[96];
  int n = snprintf(body, sizeof(body), "{\"device_id\":\"%s\"}", client->device_id);
  if (n < 1 || (size_t)n >= sizeof(body)) return LC_VOICE_CLIENT_INVALID;
  cJSON *root;
  lc_voice_client_status_t status = post(client, "/v1/session",
    "application/json", body, (size_t)n, &root);
  if (status != LC_VOICE_CLIENT_OK) return status;
  cJSON *item = unique_field(root, "session_id");
  if (!cJSON_IsString(item) || !valid_session(item->valuestring))
    status = LC_VOICE_CLIENT_SCHEMA;
  else memcpy(session_id, item->valuestring, 18u);
  cJSON_Delete(root);
  return status;
}

static uint32_t little32(const uint8_t *bytes)
{
  return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
    (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

static bool valid_pcm_wav(const uint8_t *bytes, size_t length,
                          uint32_t rate, size_t maximum)
{
  return bytes != NULL && length > 44u && length <= maximum &&
    (length - 44u) % 2u == 0u && memcmp(bytes, "RIFF", 4u) == 0 &&
    little32(bytes + 4) == length - 8u && memcmp(bytes + 8, "WAVEfmt ", 8u) == 0 &&
    little32(bytes + 16) == 16u && bytes[20] == 1u && bytes[21] == 0u &&
    bytes[22] == 1u && bytes[23] == 0u && little32(bytes + 24) == rate &&
    little32(bytes + 28) == rate * 2u && bytes[32] == 2u && bytes[33] == 0u &&
    bytes[34] == 16u && bytes[35] == 0u && memcmp(bytes + 36, "data", 4u) == 0 &&
    little32(bytes + 40) == length - 44u;
}

static bool valid_text(const char *text, size_t maximum)
{
  if (text == NULL) return false;
  const unsigned char *p = (const unsigned char *)text;
  size_t characters = 0;
  bool meaningful = false;
  while (*p != 0u)
    {
      uint32_t code = *p++;
      unsigned int more = 0u;
      uint32_t minimum = 0u;
      if (code >= 0xc2u && code <= 0xdfu) { code &= 0x1fu; more = 1u; minimum = 0x80u; }
      else if (code >= 0xe0u && code <= 0xefu) { code &= 0x0fu; more = 2u; minimum = 0x800u; }
      else if (code >= 0xf0u && code <= 0xf4u) { code &= 7u; more = 3u; minimum = 0x10000u; }
      else if (code >= 0x80u) return false;
      while (more-- != 0u)
        {
          if (*p < 0x80u || *p > 0xbfu) return false;
          code = (code << 6) | (*p++ & 0x3fu);
        }
      if (code < minimum || code > 0x10ffffu || (code >= 0xd800u && code <= 0xdfffu) ||
          code < 0x20u || code == 0x7fu || ++characters > maximum) return false;
      if (code != ' ') meaningful = true;
    }
  return meaningful;
}

lc_voice_client_status_t lc_voice_client_transcribe(lc_voice_client_t *client,
  const char *session_id, const uint8_t *wav, size_t length,
  char *transcript, size_t capacity, lc_voice_current_fn current, void *context)
{
  if (transcript == NULL || capacity == 0u) return LC_VOICE_CLIENT_INVALID;
  transcript[0] = '\0';
  if (client == NULL || !client->configured || current == NULL ||
      !valid_session(session_id) ||
      !valid_pcm_wav(wav, length, 16000u, LC_VOICE_CAPTURE_MAX))
    return LC_VOICE_CLIENT_INVALID;
  if (!current(context, session_id)) return LC_VOICE_CLIENT_CANCELLED;
  char endpoint[80];
  (void)snprintf(endpoint, sizeof(endpoint), "/v1/voice/asr?session_id=%s", session_id);
  cJSON *root;
  lc_voice_client_status_t status = post(client, endpoint, "audio/wav", wav, length, &root);
  if (!current(context, session_id))
    {
      cJSON_Delete(root);
      return LC_VOICE_CLIENT_CANCELLED;
    }
  if (status != LC_VOICE_CLIENT_OK) return status;
  cJSON *id = unique_field(root, "session_id");
  cJSON *text = unique_field(root, "transcript");
  if (!cJSON_IsString(id) || strcmp(id->valuestring, session_id) != 0 ||
      !cJSON_IsString(text) || !valid_text(text->valuestring, 500u) ||
      strlen(text->valuestring) >= capacity)
    status = LC_VOICE_CLIENT_SCHEMA;
  else memcpy(transcript, text->valuestring, strlen(text->valuestring) + 1u);
  cJSON_Delete(root);
  return status;
}

lc_voice_client_status_t lc_voice_client_synthesize(lc_voice_client_t *client,
  const char *session_id, const char *text, uint8_t *wav, size_t capacity,
  size_t *length, lc_voice_current_fn current, void *context)
{
  size_t bounded = capacity < LC_VOICE_SYNTHESIS_MAX + 2u ?
    capacity : LC_VOICE_SYNTHESIS_MAX + 2u;
  if (length != NULL) *length = 0u;
  if (wav != NULL) wipe(wav, bounded);
  if (wav == NULL || length == NULL || capacity < LC_VOICE_SYNTHESIS_MAX + 2u ||
      client == NULL || !client->configured || current == NULL ||
      !valid_session(session_id) || !valid_text(text, 72u))
    return LC_VOICE_CLIENT_INVALID;
  if (!current(context, session_id)) return LC_VOICE_CLIENT_CANCELLED;
  char path[LC_PRODUCT_BASE_PATH_MAX + 96u];
  int n = snprintf(path, sizeof(path), "%s/v1/voice/tts?session_id=%s",
    client->backend.base_path, session_id);
  if (n < 1 || (size_t)n >= sizeof(path)) return LC_VOICE_CLIENT_INVALID;
  size_t received = 0u;
  int http = client->request(client->context, &client->backend, path,
    "text/plain; charset=utf-8", client->authorization, text, strlen(text),
    (char *)wav, bounded, &received);
  lc_voice_client_status_t status;
  if (!current(context, session_id)) status = LC_VOICE_CLIENT_CANCELLED;
  else if (http < 0) status = LC_VOICE_CLIENT_NETWORK;
  else if (http < 200 || http >= 300) status = LC_VOICE_CLIENT_HTTP;
  else if (!valid_pcm_wav(wav, received, 24000u, LC_VOICE_SYNTHESIS_MAX))
    status = LC_VOICE_CLIENT_SCHEMA;
  else
    {
      *length = received;
      return LC_VOICE_CLIENT_OK;
    }
  wipe(wav, bounded);
  return status;
}

#ifdef __NuttX__
static int native_request(void *context, const lc_product_transport_t *backend,
  const char *path, const char *mime, const char *authorization,
  const void *body, size_t length, char *response, size_t capacity, size_t *received)
{
  (void)context;
  const lc_private_http_header_t headers[] = {
    {.name = "Content-Type", .value = mime},
    {.name = "Authorization", .value = authorization},
    {.name = NULL, .value = NULL}
  };
  if (backend->private_http)
    {
      const lc_private_http_header_t private_headers[] = {
        {"Content-Type", mime},
        {"Authorization", authorization},
        {NULL, NULL}
      };
      return lc_private_http_request(backend->host, backend->port, "POST", path,
        private_headers, body, length, response, capacity, received);
    }
  return lc_strict_https_request(backend->host, backend->port, "POST", path,
    headers, body, length, lc_product_trust_anchor_pem,
    lc_product_trust_anchor_pem_size, response, capacity, received);
}

bool lc_voice_client_init_default(lc_voice_client_t *client,
  const lc_product_transport_t *backend)
{
  lc_voice_client_clear(client);
  if (client == NULL) return false;
  FILE *file = fopen(LC_VOICE_TOKEN_CONFIG_PATH, "r");
  if (file == NULL) return false;
  bool valid = lc_voice_client_init_stream(client, backend, file,
    "gemini-s1", native_request, NULL);
  fclose(file);
  return valid;
}
#endif
