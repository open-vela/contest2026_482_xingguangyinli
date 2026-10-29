#include "lc_agent_bridge.h"

#include <velaclaw/client.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define LC_AGENT_CLIENT_NAME_MAX 32u

struct lc_agent_bridge_s
{
  pthread_mutex_t mutex;
  lc_agent_session_t *session;
  velaclaw_client_t *client;
  unsigned int pending_callbacks;
  bool closing;
};

struct lc_agent_callback_s
{
  lc_agent_bridge_t *bridge;
  unsigned int request_id;
};

static bool valid_client_name(const char *name)
{
  size_t index;

  if (name == NULL || name[0] == '\0')
    {
      return false;
    }

  for (index = 0u; index < LC_AGENT_CLIENT_NAME_MAX; index++)
    {
      if (name[index] == '\0')
        {
          return true;
        }
    }

  return false;
}

static void bridge_reply_callback(int status, const char *reply,
                                  void *cookie)
{
  struct lc_agent_callback_s *callback = cookie;
  lc_agent_bridge_t *bridge;
  lc_agent_status_t result;

  if (callback == NULL || callback->bridge == NULL)
    {
      return;
    }

  bridge = callback->bridge;
  pthread_mutex_lock(&bridge->mutex);
  if (!bridge->closing)
    {
      result = lc_agent_accept_reply(bridge->session, callback->request_id,
                                     status, reply);
      (void)result;
    }

  if (bridge->pending_callbacks > 0u)
    {
      bridge->pending_callbacks--;
    }
  pthread_mutex_unlock(&bridge->mutex);
  free(callback);
}

lc_agent_bridge_t *lc_agent_bridge_open(lc_agent_session_t *session,
                                        const char *client_name)
{
  lc_agent_bridge_t *bridge;

  if (session == NULL || !valid_client_name(client_name))
    {
      return NULL;
    }

  bridge = calloc(1u, sizeof(*bridge));
  if (bridge == NULL)
    {
      return NULL;
    }

  if (pthread_mutex_init(&bridge->mutex, NULL) != 0)
    {
      free(bridge);
      return NULL;
    }

  bridge->session = session;
  bridge->client = velaclaw_client_open(client_name);
  if (bridge->client == NULL)
    {
      pthread_mutex_destroy(&bridge->mutex);
      free(bridge);
      return NULL;
    }

  return bridge;
}

bool lc_agent_bridge_close(lc_agent_bridge_t *bridge)
{
  velaclaw_client_t *client;

  if (bridge == NULL)
    {
      return false;
    }

  pthread_mutex_lock(&bridge->mutex);
  if (bridge->closing || bridge->session->status == LC_AGENT_WAITING ||
      bridge->pending_callbacks > 0u)
    {
      pthread_mutex_unlock(&bridge->mutex);
      return false;
    }

  bridge->closing = true;
  client = bridge->client;
  bridge->client = NULL;
  pthread_mutex_unlock(&bridge->mutex);

  velaclaw_client_close(client);
  pthread_mutex_destroy(&bridge->mutex);
  free(bridge);
  return true;
}

lc_agent_status_t lc_agent_bridge_ask(lc_agent_bridge_t *bridge,
                                      const char *text,
                                      uint32_t now_ms,
                                      uint32_t timeout_ms)
{
  lc_agent_request_t request;
  struct lc_agent_callback_s *callback;
  velaclaw_ask_req_t transport_request;
  lc_agent_status_t result;
  int transport_result;

  if (bridge == NULL)
    {
      return LC_AGENT_INVALID;
    }

  pthread_mutex_lock(&bridge->mutex);
  if (bridge->closing)
    {
      pthread_mutex_unlock(&bridge->mutex);
      return LC_AGENT_LOCKED;
    }

  /* The flat local-client transport supports one callback at a time.  After
   * our shorter UI timeout fires, wait for that transport callback to drain
   * before accepting a retry so a late reply cannot be mistaken for it. */

  if (bridge->pending_callbacks > 0u)
    {
      pthread_mutex_unlock(&bridge->mutex);
      return LC_AGENT_BUSY;
    }

  result = lc_agent_begin(bridge->session, text, now_ms, timeout_ms,
                          &request);
  if (result != LC_AGENT_OK)
    {
      pthread_mutex_unlock(&bridge->mutex);
      return result;
    }

  callback = calloc(1u, sizeof(*callback));
  if (callback == NULL)
    {
      result = lc_agent_accept_reply(bridge->session, request.id, -1, NULL);
      pthread_mutex_unlock(&bridge->mutex);
      return result;
    }

  callback->bridge = bridge;
  callback->request_id = request.id;
  bridge->pending_callbacks++;
  pthread_mutex_unlock(&bridge->mutex);

  memset(&transport_request, 0, sizeof(transport_request));
  transport_request.text = request.text;
  transport_request.timeout_ms = (int)request.timeout_ms;
  transport_result = velaclaw_ask(bridge->client, &transport_request,
                                  bridge_reply_callback, callback);
  if (transport_result < 0)
    {
      pthread_mutex_lock(&bridge->mutex);
      result = lc_agent_accept_reply(bridge->session, callback->request_id,
                                     transport_result, NULL);
      if (bridge->pending_callbacks > 0u)
        {
          bridge->pending_callbacks--;
        }
      pthread_mutex_unlock(&bridge->mutex);
      free(callback);
      return result;
    }

  return LC_AGENT_OK;
}

lc_agent_status_t lc_agent_bridge_poll(lc_agent_bridge_t *bridge,
                                       uint32_t now_ms)
{
  lc_agent_status_t result;

  if (bridge == NULL)
    {
      return LC_AGENT_INVALID;
    }

  pthread_mutex_lock(&bridge->mutex);
  result = lc_agent_poll(bridge->session, now_ms);
  pthread_mutex_unlock(&bridge->mutex);
  return result;
}

lc_agent_status_t lc_agent_bridge_copy_reply(lc_agent_bridge_t *bridge,
                                             char *reply,
                                             size_t reply_capacity)
{
  size_t length;

  if (bridge == NULL || reply == NULL || reply_capacity == 0u)
    {
      return LC_AGENT_INVALID;
    }

  pthread_mutex_lock(&bridge->mutex);
  if (bridge->session->status != LC_AGENT_READY)
    {
      lc_agent_status_t status = bridge->session->status;
      pthread_mutex_unlock(&bridge->mutex);
      return status;
    }

  length = strlen(bridge->session->reply);
  if (length + 1u > reply_capacity)
    {
      pthread_mutex_unlock(&bridge->mutex);
      return LC_AGENT_INVALID;
    }

  memcpy(reply, bridge->session->reply, length + 1u);
  pthread_mutex_unlock(&bridge->mutex);
  return LC_AGENT_OK;
}

bool lc_agent_bridge_can_retry(lc_agent_bridge_t *bridge)
{
  bool ready;

  if (bridge == NULL)
    {
      return false;
    }

  pthread_mutex_lock(&bridge->mutex);
  ready = !bridge->closing && bridge->pending_callbacks == 0u &&
          bridge->session->status != LC_AGENT_WAITING;
  pthread_mutex_unlock(&bridge->mutex);
  return ready;
}
