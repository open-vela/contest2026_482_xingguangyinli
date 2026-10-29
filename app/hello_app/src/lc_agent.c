#include "lc_agent.h"

#include <string.h>

static bool bounded_text_length(const char *text, size_t capacity,
                                size_t *length)
{
  size_t index;

  if (text == NULL || length == NULL)
    {
      return false;
    }

  for (index = 0u; index < capacity; index++)
    {
      if (text[index] == '\0')
        {
          *length = index;
          return true;
        }
    }

  return false;
}

static bool valid_input(const char *text, size_t *length)
{
  size_t index;

  if (!bounded_text_length(text, LC_AGENT_INPUT_MAX, length) ||
      *length == 0u)
    {
      return false;
    }

  for (index = 0u; index < *length; index++)
    {
      unsigned char value = (unsigned char)text[index];
      if (value < 0x20u || value == 0x7fu)
        {
          return false;
        }
    }

  return true;
}

static bool valid_reply(const char *text, size_t *length)
{
  size_t index;

  if (!bounded_text_length(text, LC_AGENT_REPLY_MAX, length) ||
      *length == 0u)
    {
      return false;
    }

  for (index = 0u; index < *length; index++)
    {
      unsigned char value = (unsigned char)text[index];
      if (value == 0x7fu ||
          (value < 0x20u && value != '\n' && value != '\t'))
        {
          return false;
        }
    }

  return true;
}

bool lc_agent_init(lc_agent_session_t *session)
{
  if (session == NULL)
    {
      return false;
    }

  memset(session, 0, sizeof(*session));
  session->status = LC_AGENT_IDLE;
  session->next_id = 1u;
  return true;
}

lc_agent_status_t lc_agent_begin(lc_agent_session_t *session,
                                 const char *text,
                                 uint32_t now_ms,
                                 uint32_t timeout_ms,
                                 lc_agent_request_t *request)
{
  size_t length;

  if (session == NULL || request == NULL || timeout_ms == 0u ||
      timeout_ms > LC_AGENT_TIMEOUT_MAX_MS ||
      !valid_input(text, &length))
    {
      return LC_AGENT_INVALID;
    }

  if (session->status == LC_AGENT_WAITING)
    {
      return LC_AGENT_BUSY;
    }

  if (session->status == LC_AGENT_LOCKED)
    {
      return LC_AGENT_LOCKED;
    }

  memset(request, 0, sizeof(*request));
  memcpy(request->text, text, length + 1u);
  request->id = session->next_id;
  request->timeout_ms = timeout_ms;

  session->next_id++;
  if (session->next_id == 0u)
    {
      session->next_id = 1u;
    }

  session->active_id = request->id;
  session->started_ms = now_ms;
  session->timeout_ms = timeout_ms;
  session->reply[0] = '\0';
  session->status = LC_AGENT_WAITING;
  return LC_AGENT_OK;
}

lc_agent_status_t lc_agent_accept_reply(lc_agent_session_t *session,
                                        unsigned int request_id,
                                        int remote_status,
                                        const char *reply)
{
  size_t length;

  if (session == NULL)
    {
      return LC_AGENT_INVALID;
    }

  if (session->status != LC_AGENT_WAITING ||
      request_id != session->active_id)
    {
      return LC_AGENT_STALE;
    }

  if (remote_status != 0)
    {
      session->active_id = 0u;
      session->reply[0] = '\0';
      session->status = LC_AGENT_ERROR;
      return LC_AGENT_REMOTE_ERROR;
    }

  if (!valid_reply(reply, &length))
    {
      return LC_AGENT_INVALID;
    }

  memcpy(session->reply, reply, length + 1u);
  session->active_id = 0u;
  session->status = LC_AGENT_READY;
  return LC_AGENT_OK;
}

lc_agent_status_t lc_agent_poll(lc_agent_session_t *session,
                                uint32_t now_ms)
{
  if (session == NULL)
    {
      return LC_AGENT_INVALID;
    }

  if (session->status != LC_AGENT_WAITING)
    {
      return session->status;
    }

  if (now_ms < session->started_ms)
    {
      session->status = LC_AGENT_ERROR;
      session->active_id = 0u;
      return LC_AGENT_CLOCK_ERROR;
    }

  if (now_ms - session->started_ms >= session->timeout_ms)
    {
      session->status = LC_AGENT_ERROR;
      session->active_id = 0u;
      return LC_AGENT_TIMEOUT;
    }

  return LC_AGENT_WAITING;
}

lc_agent_status_t lc_agent_cancel(lc_agent_session_t *session)
{
  if (session == NULL)
    {
      return LC_AGENT_INVALID;
    }

  if (session->status != LC_AGENT_WAITING)
    {
      return session->status;
    }

  session->active_id = 0u;
  session->reply[0] = '\0';
  session->status = LC_AGENT_LOCKED;
  return LC_AGENT_CANCELLED;
}

lc_agent_status_t lc_agent_transport_reset(lc_agent_session_t *session)
{
  if (session == NULL)
    {
      return LC_AGENT_INVALID;
    }

  if (session->status == LC_AGENT_WAITING)
    {
      return LC_AGENT_BUSY;
    }

  if (session->status == LC_AGENT_LOCKED)
    {
      return LC_AGENT_LOCKED;
    }

  session->active_id = 0u;
  session->started_ms = 0u;
  session->timeout_ms = 0u;
  session->reply[0] = '\0';
  session->status = LC_AGENT_IDLE;
  return LC_AGENT_OK;
}
