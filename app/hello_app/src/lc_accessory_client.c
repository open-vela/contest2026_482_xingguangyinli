#include "lc_accessory_client.h"
#include "lc_product_trust.h"
#include "lc_strict_https.h"

#include <stdlib.h>
#include <string.h>

#ifdef __NuttX__
#  include <netutils/cJSON.h>
#else
#  include "cJSON.h"
#endif

#define LC_ACCESSORY_RESPONSE_MAX 8192u

static void wipe(void *data, size_t length)
{
  volatile unsigned char *bytes = data;
  while (length-- != 0u)
    {
      *bytes++ = 0u;
    }
}

static bool token_character(unsigned char value)
{
  return (value >= 'a' && value <= 'z') ||
         (value >= 'A' && value <= 'Z') ||
         (value >= '0' && value <= '9') ||
         value == '_' || value == '-' || value == '.';
}

static bool valid_token(const char *value)
{
  size_t length = 0u;

  if (value == NULL || strncmp(value, "sk-", 3u) == 0)
    {
      return false;
    }

  while (value[length] != '\0' && length <= 128u)
    {
      if (!token_character((unsigned char)value[length]))
        {
          return false;
        }
      length++;
    }

  return length >= 32u && length <= 128u;
}

static bool safe_backend(const lc_product_transport_t *backend)
{
  size_t index;

  if (backend == NULL || !backend->configured)
    {
      return false;
    }

  for (index = 0u; index < sizeof(backend->origin); index++)
    {
      unsigned char value = (unsigned char)backend->origin[index];
      if (value == 0u)
        {
          return true;
        }
      if (value <= 0x20u || value >= 0x7fu)
        {
          return false;
        }
    }

  return false;
}

void lc_accessory_client_clear(lc_accessory_client_t *client)
{
  if (client != NULL)
    {
      wipe(client, sizeof(*client));
    }
}

bool lc_accessory_client_init(
  lc_accessory_client_t *client,
  const lc_product_transport_t *backend,
  const char *device_token,
  lc_accessory_request_fn request,
  void *context)
{
  int written;

  if (client == NULL)
    {
      return false;
    }
  lc_accessory_client_clear(client);
  if (!safe_backend(backend) || !valid_token(device_token) || request == NULL)
    {
      return false;
    }

  client->backend = *backend;
  written = snprintf(client->authorization, sizeof(client->authorization),
                     "Bearer %s", device_token);
  if (written < 0 || (size_t)written >= sizeof(client->authorization))
    {
      lc_accessory_client_clear(client);
      return false;
    }
  client->request = request;
  client->context = context;
  client->configured = true;
  return true;
}

bool lc_accessory_client_init_stream(
  lc_accessory_client_t *client,
  const lc_product_transport_t *backend,
  FILE *stream,
  lc_accessory_request_fn request,
  void *context)
{
  char token[129] = {0};
  size_t length = 0u;
  int current;
  bool complete = false;
  bool valid;

  if (client == NULL)
    {
      return false;
    }
  lc_accessory_client_clear(client);
  if (stream == NULL)
    {
      return false;
    }

  while ((current = fgetc(stream)) != EOF)
    {
      if (current == '\n')
        {
          complete = fgetc(stream) == EOF;
          break;
        }
      if (current == '\r')
        {
          complete = fgetc(stream) == '\n' && fgetc(stream) == EOF;
          break;
        }
      if (current == 0 || length == sizeof(token) - 1u)
        {
          break;
        }
      token[length++] = (char)current;
    }
  if (current == EOF)
    {
      complete = true;
    }

  valid = complete && !ferror(stream) && lc_accessory_client_init(
    client, backend, token, request, context);
  wipe(token, sizeof(token));
  return valid;
}

static bool bounded_json(const char *bytes, size_t length)
{
  unsigned int depth = 0u;
  bool quoted = false;
  bool escaped = false;
  size_t index;

  for (index = 0u; index < length; index++)
    {
      char value = bytes[index];
      if (quoted)
        {
          if (escaped)
            {
              escaped = false;
            }
          else if (value == '\\')
            {
              escaped = true;
            }
          else if (value == '"')
            {
              quoted = false;
            }
        }
      else if (value == '"')
        {
          quoted = true;
        }
      else if (value == '{' || value == '[')
        {
          if (++depth > 8u)
            {
              return false;
            }
        }
      else if (value == '}' || value == ']')
        {
          if (depth == 0u)
            {
              return false;
            }
          depth--;
        }
    }

  return depth == 0u && !quoted;
}

static cJSON *unique_field(cJSON *object, const char *name)
{
  cJSON *found = NULL;
  cJSON *item;

  if (!cJSON_IsObject(object))
    {
      return NULL;
    }
  for (item = object->child; item != NULL; item = item->next)
    {
      if (item->string != NULL && strcmp(item->string, name) == 0)
        {
          if (found != NULL)
            {
              return NULL;
            }
          found = item;
        }
    }
  return found;
}

static size_t field_count(cJSON *object)
{
  cJSON *item;
  size_t count = 0u;

  if (!cJSON_IsObject(object))
    {
      return 0u;
    }
  for (item = object->child; item != NULL; item = item->next)
    {
      count++;
    }
  return count;
}

static bool uint32_field(cJSON *object, const char *name, uint32_t *value)
{
  cJSON *item = unique_field(object, name);
  double number;
  uint32_t converted;

  if (!cJSON_IsNumber(item))
    {
      return false;
    }
  number = item->valuedouble;
  if (number < 0.0 || number > 4294967295.0)
    {
      return false;
    }
  converted = (uint32_t)number;
  if ((double)converted != number)
    {
      return false;
    }
  *value = converted;
  return true;
}

static bool parse_event(cJSON *item, uint32_t *cursor,
                        lc_accessory_event_t *event)
{
  cJSON *device;
  cJSON *session;
  cJSON *sequence;
  cJSON *kind;
  cJSON *value;
  char wire[LC_ACCESSORY_WIRE_MAX];
  int written;

  if (field_count(item) != 6u || !uint32_field(item, "cursor", cursor) ||
      *cursor == 0u)
    {
      return false;
    }
  device = unique_field(item, "device_id");
  session = unique_field(item, "session_id");
  sequence = unique_field(item, "sequence");
  kind = unique_field(item, "event");
  value = unique_field(item, "value");
  if (!cJSON_IsString(device) || device->valuestring == NULL ||
      (strcmp(device->valuestring, "cat_a") != 0 &&
       strcmp(device->valuestring, "cat_b") != 0 &&
       strcmp(device->valuestring, "mmwave_bridge") != 0) ||
      !cJSON_IsString(session) || session->valuestring == NULL ||
      !cJSON_IsString(kind) || kind->valuestring == NULL ||
      (strcmp(kind->valuestring, "checkin") != 0 &&
       strcmp(kind->valuestring, "presence") != 0) ||
      ((strcmp(device->valuestring, "cat_a") == 0 ||
        strcmp(device->valuestring, "cat_b") == 0) &&
       strcmp(kind->valuestring, "checkin") != 0) ||
      (strcmp(device->valuestring, "mmwave_bridge") == 0 &&
       strcmp(kind->valuestring, "presence") != 0) ||
      !cJSON_IsBool(value))
    {
      return false;
    }
  uint32_t sequence_value;
  if (!uint32_field(item, "sequence", &sequence_value) || sequence_value == 0u)
    {
      return false;
    }
  (void)sequence;
  written = snprintf(wire, sizeof(wire), "LC1,%s,%s,%lu,%s,%u",
                     device->valuestring, session->valuestring,
                     (unsigned long)sequence_value, kind->valuestring,
                     cJSON_IsTrue(value) ? 1u : 0u);
  return written > 0 && (size_t)written < sizeof(wire) &&
         lc_accessory_event_parse(wire, event) == LC_ACCESSORY_OK;
}

static cJSON *parse_response(char *response, size_t length)
{
  const char *end = NULL;
  cJSON *root;

  if (length == 0u || length >= LC_ACCESSORY_RESPONSE_MAX - 1u ||
      memchr(response, '\0', length) != NULL ||
      strstr(response, "\\u0000") != NULL || !bounded_json(response, length))
    {
      return NULL;
    }
  response[length] = '\0';
  root = cJSON_ParseWithOpts(response, &end, true);
  return cJSON_IsObject(root) ? root : (cJSON_Delete(root), (cJSON *)NULL);
}

static bool cursor_reset_error(char *response, size_t length)
{
  cJSON *root = parse_response(response, length);
  cJSON *detail;
  bool valid;

  if (root == NULL)
    {
      return false;
    }
  detail = unique_field(root, "detail");
  valid = field_count(root) == 1u && cJSON_IsString(detail) &&
          detail->valuestring != NULL &&
          strcmp(detail->valuestring, "accessory_cursor_ahead") == 0;
  cJSON_Delete(root);
  return valid;
}

lc_accessory_client_status_t lc_accessory_client_poll(
  lc_accessory_client_t *client,
  lc_accessory_event_t *events,
  size_t capacity,
  size_t *count,
  bool *history_truncated)
{
  char path[LC_PRODUCT_BASE_PATH_MAX + 96u];
  char *response;
  size_t received = 0u;
  int written;
  int http;
  cJSON *root = NULL;
  cJSON *array;
  cJSON *next;
  cJSON *truncated;
  size_t event_count;
  size_t index;
  uint32_t previous;
  uint32_t next_cursor;
  lc_accessory_client_status_t status = LC_ACCESSORY_CLIENT_SCHEMA;

  if (count != NULL)
    {
      *count = 0u;
    }
  if (history_truncated != NULL)
    {
      *history_truncated = false;
    }
  if (events != NULL && capacity > 0u && capacity <= LC_ACCESSORY_POLL_MAX)
    {
      memset(events, 0, capacity * sizeof(*events));
    }
  if (client == NULL || !client->configured || events == NULL ||
      capacity == 0u || capacity > LC_ACCESSORY_POLL_MAX || count == NULL ||
      history_truncated == NULL)
    {
      return LC_ACCESSORY_CLIENT_INVALID;
    }

  written = snprintf(path, sizeof(path),
                     "%s/v1/accessories/events?after=%lu&limit=%lu",
                     client->backend.base_path,
                     (unsigned long)client->cursor,
                     (unsigned long)capacity);
  if (written < 1 || (size_t)written >= sizeof(path))
    {
      return LC_ACCESSORY_CLIENT_INVALID;
    }
  response = calloc(1u, LC_ACCESSORY_RESPONSE_MAX);
  if (response == NULL)
    {
      return LC_ACCESSORY_CLIENT_NETWORK;
    }
  http = client->request(client->context, &client->backend, path,
                         client->authorization, response,
                         LC_ACCESSORY_RESPONSE_MAX, &received);
  if (http < 0)
    {
      status = LC_ACCESSORY_CLIENT_NETWORK;
      goto done;
    }
  if (http == 409 && cursor_reset_error(response, received))
    {
      client->cursor = 0u;
      status = LC_ACCESSORY_CLIENT_RESET;
      goto done;
    }
  if (http < 200 || http >= 300)
    {
      status = LC_ACCESSORY_CLIENT_HTTP;
      goto done;
    }

  root = parse_response(response, received);
  if (root == NULL || field_count(root) != 3u)
    {
      goto done;
    }
  array = unique_field(root, "events");
  next = unique_field(root, "next_cursor");
  truncated = unique_field(root, "truncated");
  if (!cJSON_IsArray(array) || !cJSON_IsNumber(next) ||
      !cJSON_IsBool(truncated) ||
      !uint32_field(root, "next_cursor", &next_cursor))
    {
      goto done;
    }
  event_count = (size_t)cJSON_GetArraySize(array);
  if (event_count > capacity || (event_count == 0u && cJSON_IsTrue(truncated)))
    {
      goto done;
    }
  previous = client->cursor;
  for (index = 0u; index < event_count; index++)
    {
      uint32_t event_cursor;
      if (!parse_event(cJSON_GetArrayItem(array, (int)index),
                       &event_cursor, &events[index]) ||
          event_cursor <= previous ||
          (index > 0u && event_cursor != previous + 1u) ||
          (index == 0u && !cJSON_IsTrue(truncated) &&
           event_cursor != previous + 1u))
        {
          goto done;
        }
      previous = event_cursor;
    }
  if ((event_count == 0u && next_cursor != client->cursor) ||
      (event_count > 0u && next_cursor != previous))
    {
      goto done;
    }

  client->cursor = next_cursor;
  *count = event_count;
  *history_truncated = cJSON_IsTrue(truncated);
  status = LC_ACCESSORY_CLIENT_OK;

done:
  cJSON_Delete(root);
  wipe(response, LC_ACCESSORY_RESPONSE_MAX);
  free(response);
  if (status != LC_ACCESSORY_CLIENT_OK)
    {
      memset(events, 0, capacity * sizeof(*events));
      *count = 0u;
      *history_truncated = false;
    }
  return status;
}

#ifdef __NuttX__
static int native_request(
  void *context,
  const lc_product_transport_t *backend,
  const char *path,
  const char *authorization,
  char *response,
  size_t capacity,
  size_t *received)
{
  const lc_private_http_header_t headers[] = {
    {.name = "Accept", .value = "application/json"},
    {.name = "Authorization", .value = authorization},
    {.name = NULL, .value = NULL}
  };

  (void)context;
  return lc_strict_https_request(
    backend->host, backend->port, "GET", path, headers, NULL, 0u,
    lc_product_trust_anchor_pem, lc_product_trust_anchor_pem_size,
    response, capacity, received);
}

bool lc_accessory_client_init_default(
  lc_accessory_client_t *client,
  const lc_product_transport_t *backend)
{
  FILE *stream;
  bool valid;

  if (client == NULL)
    {
      return false;
    }
  lc_accessory_client_clear(client);
  stream = fopen(LC_ACCESSORY_TOKEN_CONFIG_PATH, "r");
  if (stream == NULL)
    {
      return false;
    }
  valid = lc_accessory_client_init_stream(
    client, backend, stream, native_request, NULL);
  fclose(stream);
  return valid;
}
#endif
