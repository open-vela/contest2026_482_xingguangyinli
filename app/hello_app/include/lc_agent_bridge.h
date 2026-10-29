#ifndef LIVING_CANVAS_LC_AGENT_BRIDGE_H
#define LIVING_CANVAS_LC_AGENT_BRIDGE_H

#include "lc_agent.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct lc_agent_bridge_s lc_agent_bridge_t;

lc_agent_bridge_t *lc_agent_bridge_open(lc_agent_session_t *session,
                                        const char *client_name);
bool lc_agent_bridge_close(lc_agent_bridge_t *bridge);
lc_agent_status_t lc_agent_bridge_ask(lc_agent_bridge_t *bridge,
                                      const char *text,
                                      uint32_t now_ms,
                                      uint32_t timeout_ms);
lc_agent_status_t lc_agent_bridge_poll(lc_agent_bridge_t *bridge,
                                       uint32_t now_ms);
lc_agent_status_t lc_agent_bridge_copy_reply(lc_agent_bridge_t *bridge,
                                             char *reply,
                                             size_t reply_capacity);
bool lc_agent_bridge_can_retry(lc_agent_bridge_t *bridge);

#endif
