#include "lc_product_flow.h"

#include <string.h>

static size_t bounded_length(const char *text, size_t capacity)
{
  size_t length;

  if (text == NULL)
    {
      return 0u;
    }

  for (length = 0u;
       length < capacity && text[length] != '\0';
       length++)
    {
    }

  return length;
}

static bool bounded_nonempty(const char *text, size_t capacity)
{
  size_t index;

  if (text == NULL || text[0] == '\0')
    {
      return false;
    }

  for (index = 0u; index < capacity; index++)
    {
      unsigned char value = (unsigned char)text[index];

      if (value == '\0')
        {
          return true;
        }

      if (value < 0x20u || value == 0x7fu)
        {
          return false;
        }
    }

  return false;
}

static bool valid_choice(lc_choice_id_t choice)
{
  return choice >= LC_CHOICE_TAKEOUT && choice < LC_CHOICE_COUNT;
}

static bool valid_result(const lc_product_result_t *result,
                         lc_choice_id_t expected_choice)
{
  if (result == NULL || result->choice != expected_choice ||
      !valid_choice(result->choice) ||
      (result->source != LC_PRODUCT_SOURCE_RULES &&
       result->source != LC_PRODUCT_SOURCE_MODEL))
    {
      return false;
    }

  if (!bounded_nonempty(result->name, sizeof(result->name)) ||
      !bounded_nonempty(result->reason, sizeof(result->reason)) ||
      !bounded_nonempty(result->price, sizeof(result->price)) ||
      !bounded_nonempty(result->spoken_reply, sizeof(result->spoken_reply)))
    {
      return false;
    }

  if (result->choice == LC_CHOICE_HOME && result->requires_handoff)
    {
      return false;
    }

  return true;
}

static bool valid_compact_handoff_url(const char *url)
{
  size_t index;
  size_t length;
  const char *path;

  if (url == NULL || strncmp(url, "https://", 8u) != 0)
    {
      return false;
    }

  length = bounded_length(url, LC_PRODUCT_URL_MAX);
  if (length == 0u || length >= LC_PRODUCT_URL_MAX)
    {
      return false;
    }

  for (index = 0u; index < length; index++)
    {
      unsigned char value = (unsigned char)url[index];
      if (value <= 0x20u || value == 0x7fu)
        {
          return false;
        }
    }

  path = strchr(url + 8u, '/');
  if (path == NULL || path == url + 8u || strlen(path) != 15u ||
      strncmp(path, "/c/", 3u) != 0)
    {
      return false;
    }

  for (index = 3u; index < 15u; index++)
    {
      unsigned char value = (unsigned char)path[index];
      if (!((value >= '0' && value <= '9') ||
            (value >= 'a' && value <= 'f')))
        {
          return false;
        }
    }

  return true;
}

void lc_product_flow_init(lc_product_flow_t *flow)
{
  if (flow == NULL)
    {
      return;
    }

  memset(flow, 0, sizeof(*flow));
  flow->state = LC_PRODUCT_CHOICE;
  flow->choice = LC_CHOICE_COUNT;
}

void lc_product_flow_reset(lc_product_flow_t *flow)
{
  lc_product_flow_init(flow);
}

bool lc_product_flow_start(lc_product_flow_t *flow, lc_choice_id_t choice)
{
  if (flow == NULL || flow->state != LC_PRODUCT_CHOICE ||
      !valid_choice(choice))
    {
      return false;
    }

  flow->choice = choice;
  flow->state = LC_PRODUCT_REQUESTING;
  return true;
}

bool lc_product_flow_set_recommendation(lc_product_flow_t *flow,
                                        const lc_product_result_t *result)
{
  if (flow == NULL || flow->state != LC_PRODUCT_REQUESTING ||
      !valid_result(result, flow->choice))
    {
      return false;
    }

  memcpy(&flow->result, result, sizeof(flow->result));
  flow->state = LC_PRODUCT_RECOMMENDATION;
  return true;
}

bool lc_product_flow_begin_confirmation(lc_product_flow_t *flow)
{
  if (flow == NULL || flow->state != LC_PRODUCT_RECOMMENDATION)
    {
      return false;
    }

  flow->state = LC_PRODUCT_CONFIRMING;
  return true;
}

bool lc_product_flow_set_handoff(lc_product_flow_t *flow,
                                 const char *verified_url)
{
  size_t length;

  if (flow == NULL || flow->state != LC_PRODUCT_CONFIRMING ||
      !flow->result.requires_handoff ||
      !valid_compact_handoff_url(verified_url))
    {
      return false;
    }

  length = strlen(verified_url);
  memcpy(flow->handoff_url, verified_url, length + 1u);
  flow->state = LC_PRODUCT_QR;
  return true;
}

bool lc_product_flow_complete_local(lc_product_flow_t *flow)
{
  if (flow == NULL || flow->state != LC_PRODUCT_CONFIRMING ||
      flow->result.requires_handoff)
    {
      return false;
    }

  flow->state = LC_PRODUCT_COMPLETE;
  return true;
}

void lc_product_flow_fail(lc_product_flow_t *flow,
                          lc_product_error_t error,
                          const char *message,
                          bool retryable)
{
  size_t length;

  if (flow == NULL || error == LC_PRODUCT_ERROR_NONE)
    {
      return;
    }

  flow->error_message[0] = '\0';
  if (message != NULL)
    {
      length = bounded_length(message, sizeof(flow->error_message) - 1u);
      memcpy(flow->error_message, message, length);
      flow->error_message[length] = '\0';
    }

  flow->error = error;
  flow->retryable = retryable;
  flow->state = LC_PRODUCT_ERROR;
}

bool lc_product_flow_retry(lc_product_flow_t *flow)
{
  if (flow == NULL || flow->state != LC_PRODUCT_ERROR || !flow->retryable)
    {
      return false;
    }

  lc_product_flow_reset(flow);
  return true;
}
