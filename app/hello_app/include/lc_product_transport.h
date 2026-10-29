#ifndef LC_PRODUCT_TRANSPORT_H
#define LC_PRODUCT_TRANSPORT_H

#include "lc_choice.h"
#include "lc_product_flow.h"

#include <stdbool.h>
#include <stddef.h>

#define LC_PRODUCT_TRANSPORT_CONFIG_PATH "/data/living_canvas/backend_url"
#define LC_PRODUCT_DEVICE_TOKEN_CONFIG_PATH "/data/living_canvas/voice_device_token"
#define LC_PRODUCT_HOST_MAX 96u
#define LC_PRODUCT_PORT_MAX 6u
#define LC_PRODUCT_BASE_PATH_MAX 80u
#define LC_PRODUCT_ORIGIN_MAX 192u
#define LC_PRODUCT_AUTHORIZATION_MAX 136u
#define LC_PRODUCT_SESSION_MAX 32u
#define LC_PRODUCT_TRANSCRIPT_BYTES 2001u
#define LC_PRODUCT_DEVICE_TOKEN_PREFIX "lc-owner-"

typedef enum
{
  LC_PRODUCT_TRANSPORT_OK = 0,
  LC_PRODUCT_TRANSPORT_INVALID,
  LC_PRODUCT_TRANSPORT_NOT_CONFIGURED,
  LC_PRODUCT_TRANSPORT_NETWORK,
  LC_PRODUCT_TRANSPORT_HTTP,
  LC_PRODUCT_TRANSPORT_SCHEMA,
  LC_PRODUCT_TRANSPORT_NO_CANDIDATE
} lc_product_transport_status_t;

typedef struct
{
  char host[LC_PRODUCT_HOST_MAX];
  char port[LC_PRODUCT_PORT_MAX];
  char base_path[LC_PRODUCT_BASE_PATH_MAX];
  char origin[LC_PRODUCT_ORIGIN_MAX];
  char authorization[LC_PRODUCT_AUTHORIZATION_MAX];
  bool configured;
  bool authorized;
  /* Explicit RFC1918 roadshow fallback. False always means strict HTTPS. */
  bool private_http;
} lc_product_transport_t;

bool lc_product_transport_init(lc_product_transport_t *transport,
                               const char *base_url);
/* Product-owned bearer format. Provider credentials never use this prefix. */
bool lc_product_device_token_valid(const char *device_token);
/* Owner-device token only: NEVER pass a model/provider key. No network. */
bool lc_product_transport_init_authenticated(
  lc_product_transport_t *transport, const char *base_url,
  const char *device_token);
bool lc_product_transport_init_default(lc_product_transport_t *transport);
void lc_product_transport_clear(lc_product_transport_t *transport);
bool lc_product_compact_path_matches_session(const char *compact_path,
                                             const char *session_id);
/* Worker only. Reuse the exact ASR session; never creates a new session or
 * confirms a meal. Transcript is complete, validated ASR UTF8 <=2000 bytes.
 * On any error result is cleared, including mismatched response sessions. */
lc_product_transport_status_t lc_product_transport_recommend_session(
  lc_product_transport_t *transport, const char *session_id,
  lc_choice_id_t choice, const char *transcript, lc_product_result_t *result);
/* Worker only: explicitly reject the current candidate in this same session,
 * preserving current requirements. Never creates/accepts/confirms an order. */
lc_product_transport_status_t lc_product_transport_reject_session(
  lc_product_transport_t *transport, const char *session_id,
  lc_choice_id_t choice, const char *transcript, lc_product_result_t *result);
lc_product_transport_status_t lc_product_transport_recommend(
  lc_product_transport_t *transport,
  lc_choice_id_t choice,
  const char *transcript,
  lc_product_result_t *result,
  char *session_id,
  size_t session_capacity);
lc_product_transport_status_t lc_product_transport_confirm(
  lc_product_transport_t *transport,
  const char *session_id,
  char *handoff_url,
  size_t handoff_capacity);

#endif
