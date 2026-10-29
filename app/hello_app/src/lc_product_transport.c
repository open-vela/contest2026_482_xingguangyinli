#include "lc_product_transport.h"
#include "lc_private_http.h"
#include "lc_strict_https.h"
#include "lc_product_trust.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __NuttX__
#  include <netutils/cJSON.h>
#endif

#define LC_PRODUCT_RESPONSE_MAX 8192u
#define LC_PRODUCT_REQUEST_MAX 4096u

static void wipe(void *data, size_t length)
{
  volatile unsigned char *bytes = data;
  while (length-- != 0u)
    {
      *bytes++ = 0u;
    }
}

void lc_product_transport_clear(lc_product_transport_t *transport)
{
  if (transport != NULL)
    {
      wipe(transport, sizeof(*transport));
    }
}

static bool copy_bounded(char *destination, size_t capacity,
                         const char *source)
{
  size_t length;

  if (destination == NULL || capacity == 0u || source == NULL)
    {
      return false;
    }

  length = strlen(source);
  if (length == 0u || length >= capacity)
    {
      return false;
    }

  memcpy(destination, source, length + 1u);
  return true;
}

static bool valid_host_character(unsigned char value)
{
  return isalnum(value) || value == '.' || value == '-';
}

static bool identifier(const char *value, size_t minimum, size_t maximum)
{
  size_t length = 0u;

  if (value == NULL)
    {
      return false;
    }

  while (value[length] != '\0' && length <= maximum)
    {
      unsigned char character = (unsigned char)value[length++];
      if (!((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '_' || character == '-' || character == '.'))
        {
          return false;
        }
    }

  return length >= minimum && length <= maximum;
}

static bool read_single_line(FILE *stream, char *value, size_t capacity)
{
  size_t length = 0u;
  int character;
  bool complete = false;

  if (stream == NULL || value == NULL || capacity < 2u)
    {
      return false;
    }

  memset(value, 0, capacity);
  while ((character = fgetc(stream)) != EOF)
    {
      if (character == '\n')
        {
          complete = fgetc(stream) == EOF;
          break;
        }

      if (character == '\r')
        {
          complete = fgetc(stream) == '\n' && fgetc(stream) == EOF;
          break;
        }

      if (character == 0 || length == capacity - 1u)
        {
          break;
        }

      value[length++] = (char)character;
    }

  if (character == EOF)
    {
      complete = true;
    }

  return complete && !ferror(stream) && length > 0u;
}

bool lc_product_compact_path_matches_session(const char *compact_path,
                                             const char *session_id)
{
  size_t index;

  if (compact_path == NULL || session_id == NULL ||
      strlen(compact_path) != 15u || strlen(session_id) != 17u ||
      strncmp(compact_path, "/c/", 3u) != 0 ||
      strncmp(session_id, "meal_", 5u) != 0)
    {
      return false;
    }

  for (index = 0u; index < 12u; index++)
    {
      unsigned char value = (unsigned char)compact_path[index + 3u];
      if (!((value >= '0' && value <= '9') ||
            (value >= 'a' && value <= 'f')) ||
          value != (unsigned char)session_id[index + 5u])
        {
          return false;
        }
    }

  return true;
}

bool lc_product_transport_init(lc_product_transport_t *transport,
                               const char *base_url)
{
  const char *authority;
  const char *path;
  const char *colon;
  size_t scheme_length;
  size_t authority_length;
  size_t host_length;
  size_t index;
  bool private_http;

  if (transport == NULL)
    {
      return false;
    }

  lc_product_transport_clear(transport);
  if (base_url == NULL)
    {
      return false;
    }

  if (strncmp(base_url, "https://", 8u) == 0)
    {
      scheme_length = 8u;
      private_http = false;
    }
  else if (strncmp(base_url, "http://", 7u) == 0)
    {
      scheme_length = 7u;
      private_http = true;
    }
  else
    {
      return false;
    }

  authority = base_url + scheme_length;
  path = strchr(authority, '/');
  authority_length = path == NULL ? strlen(authority)
                                  : (size_t)(path - authority);
  if (authority_length == 0u || authority_length >= LC_PRODUCT_HOST_MAX)
    {
      return false;
    }

  colon = memchr(authority, ':', authority_length);
  host_length = colon == NULL ? authority_length
                              : (size_t)(colon - authority);
  if (host_length == 0u || host_length >= sizeof(transport->host))
    {
      return false;
    }

  for (index = 0u; index < host_length; index++)
    {
      if (!valid_host_character((unsigned char)authority[index]))
        {
          return false;
        }
    }

  memcpy(transport->host, authority, host_length);
  transport->host[host_length] = '\0';
  if (private_http && !lc_private_http_host_allowed(transport->host))
    {
      lc_product_transport_clear(transport);
      return false;
    }
  if (colon != NULL)
    {
      size_t port_length = authority_length - host_length - 1u;
      unsigned long port_value = 0ul;
      if (port_length == 0u || port_length >= sizeof(transport->port))
        {
          return false;
        }

      for (index = 0u; index < port_length; index++)
        {
          if (!isdigit((unsigned char)colon[1u + index]))
            {
              return false;
            }
          port_value = port_value * 10ul +
                       (unsigned long)(colon[1u + index] - '0');
        }

      if (port_value == 0ul || port_value > 65535ul ||
          (private_http && port_value < 1024ul)) return false;

      memcpy(transport->port, colon + 1u, port_length);
      transport->port[port_length] = '\0';
    }
  else
    {
      if (private_http) return false;
      memcpy(transport->port, "443", sizeof("443"));
    }

  if (path != NULL && strcmp(path, "/") != 0)
    {
      size_t path_length = strlen(path);
      if (path_length >= sizeof(transport->base_path) ||
          strchr(path, '?') != NULL || strchr(path, '#') != NULL)
        {
          return false;
        }

      for (index = 0u; index < path_length; index++)
        {
          unsigned char value = (unsigned char)path[index];
          if (value <= 0x20u || value >= 0x7fu)
            {
              return false;
            }
        }

      memcpy(transport->base_path, path, path_length + 1u);
      while (path_length > 0u &&
             transport->base_path[path_length - 1u] == '/')
        {
          transport->base_path[--path_length] = '\0';
        }
    }

  if (!copy_bounded(transport->origin, sizeof(transport->origin),
                    base_url))
    {
      return false;
    }

  while (transport->origin[0] != '\0' &&
         transport->origin[strlen(transport->origin) - 1u] == '/')
    {
      transport->origin[strlen(transport->origin) - 1u] = '\0';
    }

  transport->private_http = private_http;
  transport->configured = true;
  return true;
}

bool lc_product_transport_init_authenticated(
  lc_product_transport_t *transport, const char *base_url,
  const char *device_token)
{
  int length;

  if (!lc_product_transport_init(transport, base_url) ||
      !lc_product_device_token_valid(device_token))
    {
      lc_product_transport_clear(transport);
      return false;
    }

  length = snprintf(transport->authorization,
                    sizeof(transport->authorization),
                    "Bearer %s", device_token);
  if (length <= 0 ||
      (size_t)length >= sizeof(transport->authorization))
    {
      lc_product_transport_clear(transport);
      return false;
    }

  transport->authorized = true;
  return true;
}

bool lc_product_device_token_valid(const char *device_token)
{
  return identifier(device_token, 32u, 128u) &&
         strncmp(device_token, LC_PRODUCT_DEVICE_TOKEN_PREFIX,
                 sizeof(LC_PRODUCT_DEVICE_TOKEN_PREFIX) - 1u) == 0;
}

bool lc_product_transport_init_default(lc_product_transport_t *transport)
{
  FILE *url_stream;
  FILE *token_stream;
  char url[LC_PRODUCT_ORIGIN_MAX];
  char token[129];
  bool valid;

  if (transport == NULL)
    {
      return false;
    }

  lc_product_transport_clear(transport);
  url_stream = fopen(LC_PRODUCT_TRANSPORT_CONFIG_PATH, "r");
  if (url_stream == NULL)
    {
      return false;
    }

  valid = read_single_line(url_stream, url, sizeof(url));
  fclose(url_stream);
  if (!valid)
    {
      return false;
    }

  token_stream = fopen(LC_PRODUCT_DEVICE_TOKEN_CONFIG_PATH, "r");
  if (token_stream == NULL)
    {
      wipe(url, sizeof(url));
      return false;
    }

  valid = read_single_line(token_stream, token, sizeof(token));
  fclose(token_stream);
  if (valid)
    {
      valid = lc_product_transport_init_authenticated(transport, url, token);
    }

  wipe(token, sizeof(token));
  wipe(url, sizeof(url));
  return valid;
}

#ifdef __NuttX__
static bool valid_session_id(const char *session_id)
{
  if (session_id == NULL || strlen(session_id) != 17u ||
      strncmp(session_id, "meal_", 5u) != 0) return false;
  for (size_t i = 5u; i < 17u; i++)
    if (!((session_id[i] >= '0' && session_id[i] <= '9') ||
          (session_id[i] >= 'a' && session_id[i] <= 'f'))) return false;
  return true;
}

static bool build_path(const lc_product_transport_t *transport,
                       const char *endpoint,
                       char *path,
                       size_t capacity)
{
  int length;

  length = snprintf(path, capacity, "%s%s", transport->base_path, endpoint);
  return length > 0 && (size_t)length < capacity;
}

/* Keep cJSON recursion bounded and ignore bracket characters inside strings. */
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
          if (escaped) escaped = false;
          else if (value == '\\') escaped = true;
          else if (value == '"') quoted = false;
        }
      else if (value == '"') quoted = true;
      else if (value == '{' || value == '[')
        {
          if (++depth > 16u) return false;
        }
      else if (value == '}' || value == ']')
        {
          if (depth == 0u) return false;
          depth--;
        }
    }
  return depth == 0u && !quoted;
}

static lc_product_transport_status_t request_json(
  lc_product_transport_t *transport,
  const char *endpoint,
  const char *request_body,
  char *response,
  size_t response_capacity)
{
  lc_private_http_header_t headers[3];
  char path[LC_PRODUCT_BASE_PATH_MAX + 32u];
  size_t response_length = 0u;
  int status;

  if (transport == NULL || !transport->configured || !transport->authorized ||
      transport->authorization[0] == '\0' || endpoint == NULL ||
      request_body == NULL || response == NULL || response_capacity < 2u ||
      !build_path(transport, endpoint, path, sizeof(path)))
    {
      return LC_PRODUCT_TRANSPORT_INVALID;
    }

  headers[0].name = "Content-Type";
  headers[0].value = "application/json";
  headers[1].name = "Authorization";
  headers[1].value = transport->authorization;
  headers[2].name = NULL;
  headers[2].value = NULL;
  memset(response, 0, response_capacity);
  if (transport->private_http)
    {
      status = lc_private_http_request(
        transport->host, transport->port, "POST", path, headers,
        request_body, strlen(request_body), response, response_capacity,
        &response_length);
    }
  else
    {
      status = lc_strict_https_request(
        transport->host, transport->port, "POST", path, headers, request_body,
        strlen(request_body), lc_product_trust_anchor_pem,
        lc_product_trust_anchor_pem_size, response, response_capacity,
        &response_length);
    }
  if (status < 0)
    {
      return LC_PRODUCT_TRANSPORT_NETWORK;
    }

  if (status < 200 || status >= 300)
    {
      /* Exact bounded FastAPI error contract: do not parse an arbitrary error
       * document or confuse state-conflict 409 with exhausted candidates. */
      static const char exhausted[] = "{\"detail\":\"no_safe_candidate\"}";
      if (status == 409 && strcmp(endpoint, "/v1/dialogue") == 0 &&
          response_length == sizeof(exhausted) - 1u &&
          response_length < response_capacity &&
          memcmp(response, exhausted, sizeof(exhausted) - 1u) == 0)
        return LC_PRODUCT_TRANSPORT_NO_CANDIDATE;
      return LC_PRODUCT_TRANSPORT_HTTP;
    }

  if (response_length == 0u ||
      response_length >= response_capacity - 1u ||
      memchr(response, '\0', response_length) != NULL)
    {
      return LC_PRODUCT_TRANSPORT_SCHEMA;
    }

  response[response_length] = '\0';
  if (strstr(response, "\\u0000") != NULL ||
      !bounded_json(response, response_length))
    {
      return LC_PRODUCT_TRANSPORT_SCHEMA;
    }
  return LC_PRODUCT_TRANSPORT_OK;
}

static cJSON *parse_json_document(const char *response)
{
  const char *parse_end = NULL;

  if (response == NULL)
    {
      return NULL;
    }

  return cJSON_ParseWithOpts(response, &parse_end, true);
}

static bool json_string(cJSON *object, const char *name,
                        char *destination, size_t capacity)
{
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  return cJSON_IsString(item) && item->valuestring != NULL &&
         copy_bounded(destination, capacity, item->valuestring);
}

static const char *choice_value(lc_choice_id_t choice)
{
  switch (choice)
    {
      case LC_CHOICE_TAKEOUT:
        return "takeout";
      case LC_CHOICE_MYSTERY:
        return "mystery";
      case LC_CHOICE_HOME:
        return "home";
      default:
        return NULL;
    }
}

static lc_product_transport_status_t create_session(
  lc_product_transport_t *transport,
  char *session_id,
  size_t session_capacity)
{
  char response[512];
  lc_product_transport_status_t status;
  cJSON *root;
  bool valid;

  status = request_json(transport, "/v1/session",
                        "{\"device_id\":\"gemini-s1\"}",
                        response, sizeof(response));
  if (status != LC_PRODUCT_TRANSPORT_OK)
    {
      return status;
    }

  root = parse_json_document(response);
  if (root == NULL)
    {
      return LC_PRODUCT_TRANSPORT_SCHEMA;
    }

  valid = json_string(root, "session_id", session_id, session_capacity) &&
          valid_session_id(session_id);
  cJSON_Delete(root);
  return valid ? LC_PRODUCT_TRANSPORT_OK : LC_PRODUCT_TRANSPORT_SCHEMA;
}

static char *build_dialogue_body(const char *session_id,
                                 lc_choice_id_t choice,
                                 const char *transcript, bool reject_current)
{
  cJSON *root;
  char *body;
  const char *choice_text = choice_value(choice);

  if (choice_text == NULL || transcript == NULL || transcript[0] == '\0')
    {
      return NULL;
    }

  root = cJSON_CreateObject();
  if (root == NULL ||
      cJSON_AddStringToObject(root, "session_id", session_id) == NULL ||
      cJSON_AddStringToObject(root, "choice", choice_text) == NULL ||
      cJSON_AddStringToObject(root, "transcript", transcript) == NULL ||
      (reject_current && cJSON_AddBoolToObject(root, "reject_current", true) == NULL))
    {
      cJSON_Delete(root);
      return NULL;
    }

  body = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return body;
}

static bool parse_recommendation(const char *response,
                                 const char *session_id,
                                 lc_choice_id_t choice,
                                 lc_product_result_t *result)
{
  cJSON *root = parse_json_document(response);
  cJSON *recommendation;
  cJSON *candidate;
  cJSON *source;
  cJSON *fallback;
  cJSON *handoff;
  cJSON *price;
  bool valid = false;

  if (!cJSON_IsObject(root))
    {
      cJSON_Delete(root);
      return false;
    }
  cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "session_id");
  if (!cJSON_IsString(id) || id->valuestring == NULL ||
      strcmp(id->valuestring, session_id) != 0)
    {
      cJSON_Delete(root);
      return false;
    }

  recommendation = cJSON_GetObjectItemCaseSensitive(root, "recommendation");
  candidate = cJSON_IsObject(recommendation)
                ? cJSON_GetObjectItemCaseSensitive(recommendation,
                                                   "candidate")
                : NULL;
  source = cJSON_IsObject(recommendation)
             ? cJSON_GetObjectItemCaseSensitive(recommendation, "source")
             : NULL;
  fallback = cJSON_IsObject(recommendation)
               ? cJSON_GetObjectItemCaseSensitive(recommendation,
                                                  "rules_fallback")
               : NULL;
  handoff = cJSON_GetObjectItemCaseSensitive(root, "requires_handoff");
  price = cJSON_IsObject(candidate)
            ? cJSON_GetObjectItemCaseSensitive(candidate, "price_cny")
            : NULL;
  memset(result, 0, sizeof(*result));
  result->choice = choice;
  if (cJSON_IsString(source) && source->valuestring != NULL)
    {
      result->source = strcmp(source->valuestring, "model") == 0
                         ? LC_PRODUCT_SOURCE_MODEL
                         : LC_PRODUCT_SOURCE_RULES;
    }

  result->rules_fallback = cJSON_IsTrue(fallback);
  result->requires_handoff = cJSON_IsTrue(handoff);
  if (cJSON_IsNumber(price))
    {
      (void)snprintf(result->price, sizeof(result->price), "¥%.0f",
                     price->valuedouble);
    }

  valid = cJSON_IsObject(candidate) && cJSON_IsObject(recommendation) &&
          json_string(candidate, "name", result->name,
                      sizeof(result->name)) &&
          json_string(recommendation, "reason", result->reason,
                      sizeof(result->reason)) &&
          result->price[0] != '\0' &&
          json_string(root, "spoken_reply", result->spoken_reply,
                      sizeof(result->spoken_reply));
  cJSON_Delete(root);
  return valid;
}

static lc_product_transport_status_t dialogue_session(
  lc_product_transport_t *transport, const char *session_id,
  lc_choice_id_t choice, const char *transcript, lc_product_result_t *result,
  bool reject_current)
{
  char *response;
  char *body;
  lc_product_transport_status_t status;
  bool valid;

  if (result != NULL) memset(result, 0, sizeof(*result));
  if (transport == NULL || !transport->configured || !transport->authorized ||
      result == NULL ||
      !valid_session_id(session_id) ||
      choice_value(choice) == NULL || transcript == NULL ||
      transcript[0] == '\0' || strlen(transcript) >= LC_PRODUCT_TRANSCRIPT_BYTES)
    {
      return LC_PRODUCT_TRANSPORT_INVALID;
    }

  body = build_dialogue_body(session_id, choice, transcript, reject_current);
  if (body == NULL || strlen(body) >= LC_PRODUCT_REQUEST_MAX)
    {
      cJSON_free(body);
      return LC_PRODUCT_TRANSPORT_INVALID;
    }

  response = malloc(LC_PRODUCT_RESPONSE_MAX);
  if (response == NULL)
    {
      cJSON_free(body);
      return LC_PRODUCT_TRANSPORT_NETWORK;
    }

  status = request_json(transport, "/v1/dialogue", body,
                        response, LC_PRODUCT_RESPONSE_MAX);
  cJSON_free(body);
  if (status != LC_PRODUCT_TRANSPORT_OK)
    {
      free(response);
      return status;
    }

  valid = parse_recommendation(response, session_id, choice, result);
  free(response);
  if (!valid) memset(result, 0, sizeof(*result));
  return valid ? LC_PRODUCT_TRANSPORT_OK : LC_PRODUCT_TRANSPORT_SCHEMA;
}

lc_product_transport_status_t lc_product_transport_recommend_session(
  lc_product_transport_t *transport, const char *session_id,
  lc_choice_id_t choice, const char *transcript, lc_product_result_t *result)
{
  return dialogue_session(transport, session_id, choice, transcript, result, false);
}

lc_product_transport_status_t lc_product_transport_reject_session(
  lc_product_transport_t *transport, const char *session_id,
  lc_choice_id_t choice, const char *transcript, lc_product_result_t *result)
{
  return dialogue_session(transport, session_id, choice, transcript, result, true);
}

lc_product_transport_status_t lc_product_transport_recommend(
  lc_product_transport_t *transport, lc_choice_id_t choice,
  const char *transcript, lc_product_result_t *result,
  char *session_id, size_t session_capacity)
{
  if (result != NULL) memset(result, 0, sizeof(*result));
  if (session_id != NULL && session_capacity > 0u) session_id[0] = '\0';
  if (transport == NULL || !transport->configured || !transport->authorized ||
      result == NULL ||
      session_id == NULL || session_capacity < LC_PRODUCT_SESSION_MAX ||
      choice_value(choice) == NULL || transcript == NULL ||
      transcript[0] == '\0' || strlen(transcript) >= LC_PRODUCT_TRANSCRIPT_BYTES)
    return LC_PRODUCT_TRANSPORT_INVALID;
  lc_product_transport_status_t status = create_session(transport, session_id, session_capacity);
  if (status != LC_PRODUCT_TRANSPORT_OK) return status;
  return lc_product_transport_recommend_session(transport, session_id, choice, transcript, result);
}

static lc_product_transport_status_t post_session_action(
  lc_product_transport_t *transport,
  const char *endpoint,
  const char *session_id,
  const char *field,
  const char *value,
  char *response,
  size_t response_capacity)
{
  cJSON *root = cJSON_CreateObject();
  char *body;
  lc_product_transport_status_t status;

  if (root == NULL ||
      cJSON_AddStringToObject(root, "session_id", session_id) == NULL ||
      cJSON_AddStringToObject(root, field, value) == NULL)
    {
      cJSON_Delete(root);
      return LC_PRODUCT_TRANSPORT_INVALID;
    }

  body = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (body == NULL)
    {
      return LC_PRODUCT_TRANSPORT_INVALID;
    }

  status = request_json(transport, endpoint, body,
                        response, response_capacity);
  cJSON_free(body);
  return status;
}

lc_product_transport_status_t lc_product_transport_confirm(
  lc_product_transport_t *transport,
  const char *session_id,
  char *handoff_url,
  size_t handoff_capacity)
{
  char *response;
  char compact_path[80];
  cJSON *root;
  lc_product_transport_status_t status;
  int length;

  if (handoff_url != NULL && handoff_capacity > 0u)
    {
      handoff_url[0] = '\0';
    }

  if (transport == NULL || !transport->configured || !transport->authorized ||
      session_id == NULL ||
      strncmp(session_id, "meal_", 5u) != 0 || handoff_url == NULL ||
      handoff_capacity == 0u)
    {
      return LC_PRODUCT_TRANSPORT_INVALID;
    }

  response = malloc(LC_PRODUCT_RESPONSE_MAX);
  if (response == NULL)
    {
      return LC_PRODUCT_TRANSPORT_NETWORK;
    }

  status = post_session_action(transport, "/v1/device/event", session_id,
                               "event", "accept_candidate", response,
                               LC_PRODUCT_RESPONSE_MAX);
  if (status != LC_PRODUCT_TRANSPORT_OK)
    {
      free(response);
      return status;
    }

  status = post_session_action(transport, "/v1/confirm", session_id,
                               "platform", "eleme", response,
                               LC_PRODUCT_RESPONSE_MAX);
  if (status != LC_PRODUCT_TRANSPORT_OK)
    {
      free(response);
      return status;
    }

  root = parse_json_document(response);
  if (root == NULL ||
      !json_string(root, "compact_path", compact_path,
                   sizeof(compact_path)) ||
      !lc_product_compact_path_matches_session(compact_path, session_id))
    {
      cJSON_Delete(root);
      free(response);
      return LC_PRODUCT_TRANSPORT_SCHEMA;
    }

  cJSON_Delete(root);
  free(response);
  length = snprintf(handoff_url, handoff_capacity, "%s%s", transport->origin,
                    compact_path);
  if (length <= 0 || (size_t)length >= handoff_capacity)
    {
      handoff_url[0] = '\0';
      return LC_PRODUCT_TRANSPORT_SCHEMA;
    }

  return LC_PRODUCT_TRANSPORT_OK;
}
#else
lc_product_transport_status_t lc_product_transport_reject_session(
  lc_product_transport_t *transport, const char *session_id,
  lc_choice_id_t choice, const char *transcript, lc_product_result_t *result)
{
  (void)transport;
  (void)session_id;
  (void)choice;
  (void)transcript;
  if (result != NULL) memset(result, 0, sizeof(*result));
  return LC_PRODUCT_TRANSPORT_NOT_CONFIGURED;
}

lc_product_transport_status_t lc_product_transport_recommend_session(
  lc_product_transport_t *transport, const char *session_id,
  lc_choice_id_t choice, const char *transcript, lc_product_result_t *result)
{
  (void)transport;
  (void)session_id;
  (void)choice;
  (void)transcript;
  if (result != NULL) memset(result, 0, sizeof(*result));
  return LC_PRODUCT_TRANSPORT_NOT_CONFIGURED;
}

lc_product_transport_status_t lc_product_transport_recommend(
  lc_product_transport_t *transport,
  lc_choice_id_t choice,
  const char *transcript,
  lc_product_result_t *result,
  char *session_id,
  size_t session_capacity)
{
  (void)transport;
  (void)choice;
  (void)transcript;
  (void)result;
  (void)session_id;
  (void)session_capacity;
  return LC_PRODUCT_TRANSPORT_NOT_CONFIGURED;
}

lc_product_transport_status_t lc_product_transport_confirm(
  lc_product_transport_t *transport,
  const char *session_id,
  char *handoff_url,
  size_t handoff_capacity)
{
  (void)transport;
  (void)session_id;
  (void)handoff_url;
  (void)handoff_capacity;
  return LC_PRODUCT_TRANSPORT_NOT_CONFIGURED;
}
#endif
