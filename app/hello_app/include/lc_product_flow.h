#ifndef LC_PRODUCT_FLOW_H
#define LC_PRODUCT_FLOW_H

#include "lc_choice.h"

#include <stdbool.h>
#include <stddef.h>

#define LC_PRODUCT_NAME_MAX 64u
#define LC_PRODUCT_REASON_MAX 160u
#define LC_PRODUCT_PRICE_MAX 40u
#define LC_PRODUCT_SPEECH_MAX 256u
#define LC_PRODUCT_URL_MAX 192u
#define LC_PRODUCT_ERROR_MESSAGE_MAX 96u

typedef enum
{
  LC_PRODUCT_CHOICE = 0,
  LC_PRODUCT_REQUESTING,
  LC_PRODUCT_RECOMMENDATION,
  LC_PRODUCT_CONFIRMING,
  LC_PRODUCT_QR,
  LC_PRODUCT_COMPLETE,
  LC_PRODUCT_ERROR
} lc_product_state_t;

typedef enum
{
  LC_PRODUCT_SOURCE_RULES = 0,
  LC_PRODUCT_SOURCE_MODEL
} lc_product_source_t;

typedef enum
{
  LC_PRODUCT_ERROR_NONE = 0,
  LC_PRODUCT_ERROR_NETWORK,
  LC_PRODUCT_ERROR_AGENT,
  LC_PRODUCT_ERROR_SCHEMA,
  LC_PRODUCT_ERROR_HANDOFF,
  LC_PRODUCT_ERROR_NO_CANDIDATE
} lc_product_error_t;

typedef struct
{
  lc_choice_id_t choice;
  lc_product_source_t source;
  bool requires_handoff;
  bool rules_fallback;
  char name[LC_PRODUCT_NAME_MAX];
  char reason[LC_PRODUCT_REASON_MAX];
  char price[LC_PRODUCT_PRICE_MAX];
  char spoken_reply[LC_PRODUCT_SPEECH_MAX];
} lc_product_result_t;

typedef struct
{
  lc_product_state_t state;
  lc_choice_id_t choice;
  lc_product_result_t result;
  char handoff_url[LC_PRODUCT_URL_MAX];
  lc_product_error_t error;
  char error_message[LC_PRODUCT_ERROR_MESSAGE_MAX];
  bool retryable;
} lc_product_flow_t;

void lc_product_flow_init(lc_product_flow_t *flow);
void lc_product_flow_reset(lc_product_flow_t *flow);
bool lc_product_flow_start(lc_product_flow_t *flow, lc_choice_id_t choice);
bool lc_product_flow_set_recommendation(lc_product_flow_t *flow,
                                        const lc_product_result_t *result);
bool lc_product_flow_begin_confirmation(lc_product_flow_t *flow);
bool lc_product_flow_set_handoff(lc_product_flow_t *flow,
                                 const char *verified_url);
bool lc_product_flow_complete_local(lc_product_flow_t *flow);
void lc_product_flow_fail(lc_product_flow_t *flow,
                          lc_product_error_t error,
                          const char *message,
                          bool retryable);
bool lc_product_flow_retry(lc_product_flow_t *flow);

#endif
