#ifndef LC_PRODUCT_INTENT_H
#define LC_PRODUCT_INTENT_H

#include "lc_choice.h"

#include <stdbool.h>

bool lc_product_intent_classify(const char *transcript,
                                lc_choice_id_t *choice);

typedef enum
{
  LC_PRODUCT_VOICE_UNRESOLVED = 0, LC_PRODUCT_VOICE_REQUEST,
  LC_PRODUCT_VOICE_REJECT, LC_PRODUCT_VOICE_CONFIRM
} lc_product_voice_action_t;
/* No model-generated authorization. Confirmation requires an existing
 * candidate and a complete explicit phrase, never a substring or vague yes.
 * Dietary-only requests use the selected mode (takeout when none selected). */
lc_product_voice_action_t lc_product_intent_route(const char *transcript,
  lc_choice_id_t selected, bool has_candidate, lc_choice_id_t *choice);

#endif
