#include "lc_agent.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void fill_text(char *buffer, size_t size, char value)
{
  memset(buffer, value, size - 1u);
  buffer[size - 1u] = '\0';
}

int main(void)
{
  lc_agent_session_t session;
  lc_agent_session_t timeout_session;
  lc_agent_session_t clock_session;
  lc_agent_request_t request;
  unsigned int first_id;
  char long_input[LC_AGENT_INPUT_MAX + 1u];
  char long_reply[LC_AGENT_REPLY_MAX + 1u];

  assert(!lc_agent_init(NULL));
  assert(lc_agent_init(&session));
  assert(session.status == LC_AGENT_IDLE);
  assert(session.reply[0] == '\0');

  assert(lc_agent_begin(NULL, "dinner", 100u, 1000u, &request) ==
         LC_AGENT_INVALID);
  assert(lc_agent_begin(&session, NULL, 100u, 1000u, &request) ==
         LC_AGENT_INVALID);
  assert(lc_agent_begin(&session, "", 100u, 1000u, &request) ==
         LC_AGENT_INVALID);
  assert(lc_agent_begin(&session, "   \t", 100u, 1000u, &request) ==
         LC_AGENT_INVALID);
  assert(lc_agent_begin(&session, "line\nbreak", 100u, 1000u, &request) ==
         LC_AGENT_INVALID);
  assert(lc_agent_begin(&session, "dinner", 100u, 0u, &request) ==
         LC_AGENT_INVALID);
  assert(lc_agent_begin(&session, "dinner", 100u,
                        LC_AGENT_TIMEOUT_MAX_MS + 1u, &request) ==
         LC_AGENT_INVALID);

  fill_text(long_input, sizeof(long_input), 'a');
  assert(lc_agent_begin(&session, long_input, 100u, 1000u, &request) ==
         LC_AGENT_INVALID);

  assert(lc_agent_begin(&session, "Recommend dinner", 1000u, 5000u,
                        &request) == LC_AGENT_OK);
  assert(lc_agent_transport_reset(&session) == LC_AGENT_BUSY);
  assert(session.status == LC_AGENT_WAITING);
  assert(strcmp(request.text, "Recommend dinner") == 0);
  assert(request.timeout_ms == 5000u);
  assert(request.id != 0u);
  first_id = request.id;

  assert(lc_agent_begin(&session, "second", 1001u, 5000u, &request) ==
         LC_AGENT_BUSY);
  assert(lc_agent_accept_reply(&session, first_id + 1u, 0,
                               "wrong request") == LC_AGENT_STALE);
  assert(session.status == LC_AGENT_WAITING);

  fill_text(long_reply, sizeof(long_reply), 'b');
  assert(lc_agent_accept_reply(&session, first_id, 0, long_reply) ==
         LC_AGENT_INVALID);
  assert(session.status == LC_AGENT_WAITING);
  assert(lc_agent_accept_reply(&session, first_id, 0, "unsafe\x1b[31m") ==
         LC_AGENT_INVALID);
  assert(session.status == LC_AGENT_WAITING);

  assert(lc_agent_poll(&session, 5999u) == LC_AGENT_WAITING);
  assert(lc_agent_accept_reply(&session, first_id, 0,
                               "Two safe options") == LC_AGENT_OK);
  assert(session.status == LC_AGENT_READY);
  assert(strcmp(session.reply, "Two safe options") == 0);

  assert(lc_agent_begin(&session, "new turn", 7000u, 1000u, &request) ==
         LC_AGENT_OK);
  assert(session.reply[0] == '\0');
  assert(request.id != first_id);
  assert(lc_agent_accept_reply(&session, request.id, -1, "ignored") ==
         LC_AGENT_REMOTE_ERROR);
  assert(session.status == LC_AGENT_ERROR);
  assert(session.reply[0] == '\0');

  assert(lc_agent_begin(&session, "retry", 8000u, 1000u, &request) ==
         LC_AGENT_OK);
  assert(lc_agent_cancel(&session) == LC_AGENT_CANCELLED);
  assert(session.status == LC_AGENT_LOCKED);
  assert(lc_agent_accept_reply(&session, request.id, 0, "late") ==
         LC_AGENT_STALE);
  assert(lc_agent_begin(&session, "unsafe retry", 9000u, 1000u,
                        &request) == LC_AGENT_LOCKED);

  assert(lc_agent_transport_reset(&session) == LC_AGENT_LOCKED);
  assert(lc_agent_init(&timeout_session));
  assert(lc_agent_begin(&timeout_session, "timeout", 10000u, 1000u, &request) ==
         LC_AGENT_OK);
  assert(lc_agent_poll(&timeout_session, 10999u) == LC_AGENT_WAITING);
  assert(lc_agent_poll(&timeout_session, 11000u) == LC_AGENT_TIMEOUT);
  assert(timeout_session.status == LC_AGENT_ERROR);
  assert(lc_agent_accept_reply(&timeout_session, request.id, 0, "late") ==
         LC_AGENT_STALE);
  assert(lc_agent_begin(&timeout_session, "retry after timeout", 12000u,
                        1000u, &request) == LC_AGENT_OK);
  assert(lc_agent_accept_reply(&timeout_session, request.id, 0,
                               "recovered") == LC_AGENT_OK);
  assert(strcmp(timeout_session.reply, "recovered") == 0);
  assert(lc_agent_init(&clock_session));
  assert(lc_agent_begin(&clock_session, "clock", 12000u, 1000u, &request) ==
         LC_AGENT_OK);
  assert(lc_agent_poll(&clock_session, 11999u) == LC_AGENT_CLOCK_ERROR);
  assert(clock_session.status == LC_AGENT_ERROR);

  puts("PASS: lc_agent");
  return 0;
}
