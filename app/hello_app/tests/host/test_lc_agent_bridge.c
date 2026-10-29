#include "lc_agent_bridge.h"

#include <velaclaw/client.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct velaclaw_client_s
{
  int marker;
};

static struct velaclaw_client_s g_fake_client = {42};
static bool g_open_fails;
static int g_ask_result;
static int g_open_count;
static int g_close_count;
static int g_ask_count;
static char g_client_name[32];
static char g_request_text[LC_AGENT_INPUT_MAX];
static int g_request_timeout_ms;
static void (*g_callback)(int, const char *, void *);
static void *g_cookie;

velaclaw_client_t *velaclaw_client_open(const char *name)
{
  g_open_count++;
  if (g_open_fails)
    {
      return NULL;
    }

  snprintf(g_client_name, sizeof(g_client_name), "%s", name);
  return &g_fake_client;
}

void velaclaw_client_close(velaclaw_client_t *client)
{
  assert(client == &g_fake_client);
  g_close_count++;
}

int velaclaw_ask(velaclaw_client_t *client,
                 const velaclaw_ask_req_t *request,
                 void (*callback)(int, const char *, void *),
                 void *cookie)
{
  assert(client == &g_fake_client);
  g_ask_count++;
  snprintf(g_request_text, sizeof(g_request_text), "%s", request->text);
  g_request_timeout_ms = request->timeout_ms;
  g_callback = callback;
  g_cookie = cookie;
  return g_ask_result;
}

static void reset_fake(void)
{
  g_open_fails = false;
  g_ask_result = 0;
  g_open_count = 0;
  g_close_count = 0;
  g_ask_count = 0;
  g_client_name[0] = '\0';
  g_request_text[0] = '\0';
  g_request_timeout_ms = 0;
  g_callback = NULL;
  g_cookie = NULL;
}

int main(void)
{
  lc_agent_session_t session;
  lc_agent_bridge_t *bridge;
  void (*late_callback)(int, const char *, void *);
  void *late_cookie;
  char reply[64];

  reset_fake();
  assert(lc_agent_init(&session));
  assert(lc_agent_bridge_open(NULL, "living_canvas") == NULL);
  assert(lc_agent_bridge_open(&session, NULL) == NULL);

  bridge = lc_agent_bridge_open(&session, "living_canvas");
  assert(bridge != NULL);
  assert(g_open_count == 1);
  assert(strcmp(g_client_name, "living_canvas") == 0);

  assert(lc_agent_bridge_ask(bridge, "Recommend dinner", 1000u, 5000u) ==
         LC_AGENT_OK);
  assert(g_ask_count == 1);
  assert(strcmp(g_request_text, "Recommend dinner") == 0);
  assert(g_request_timeout_ms == 5000);
  assert(lc_agent_bridge_ask(bridge, "second", 1001u, 5000u) ==
         LC_AGENT_BUSY);
  assert(g_ask_count == 1);
  assert(g_callback != NULL);

  g_callback(0, "Two safe options", g_cookie);
  assert(lc_agent_bridge_copy_reply(bridge, reply, sizeof(reply)) ==
         LC_AGENT_OK);
  assert(strcmp(reply, "Two safe options") == 0);
  assert(lc_agent_bridge_close(bridge));
  assert(g_close_count == 1);

  reset_fake();
  assert(lc_agent_init(&session));
  bridge = lc_agent_bridge_open(&session, "living_canvas");
  assert(bridge != NULL);
  g_ask_result = -5;
  assert(lc_agent_bridge_ask(bridge, "fallback", 2000u, 1000u) ==
         LC_AGENT_REMOTE_ERROR);
  assert(session.status == LC_AGENT_ERROR);
  assert(lc_agent_bridge_close(bridge));

  reset_fake();
  assert(lc_agent_init(&session));
  bridge = lc_agent_bridge_open(&session, "living_canvas");
  assert(bridge != NULL);
  assert(lc_agent_bridge_ask(bridge, "timeout", 3000u, 1000u) ==
         LC_AGENT_OK);
  late_callback = g_callback;
  late_cookie = g_cookie;
  assert(lc_agent_bridge_poll(bridge, 4000u) == LC_AGENT_TIMEOUT);
  assert(!lc_agent_bridge_can_retry(bridge));
  assert(!lc_agent_bridge_close(bridge));
  assert(lc_agent_bridge_ask(bridge, "too early", 4001u, 1000u) ==
         LC_AGENT_BUSY);
  late_callback(0, "late reply", late_cookie);
  assert(lc_agent_bridge_can_retry(bridge));
  assert(lc_agent_bridge_ask(bridge, "retry after timeout", 4001u, 1000u) ==
         LC_AGENT_OK);
  assert(strcmp(g_request_text, "retry after timeout") == 0);
  g_callback(0, "fresh reply", g_cookie);
  assert(lc_agent_bridge_copy_reply(bridge, reply, sizeof(reply)) ==
         LC_AGENT_OK);
  assert(strcmp(reply, "fresh reply") == 0);
  assert(lc_agent_bridge_close(bridge));

  reset_fake();
  assert(lc_agent_init(&session));
  g_open_fails = true;
  assert(lc_agent_bridge_open(&session, "living_canvas") == NULL);

  puts("PASS: lc_agent_bridge");
  return 0;
}
