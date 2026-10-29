#ifndef LIVING_CANVAS_LC_PRODUCT_DIALOGUE_H
#define LIVING_CANVAS_LC_PRODUCT_DIALOGUE_H

#include "lc_product_flow.h"

#include <stdbool.h>
#include <stdint.h>

#define LC_PRODUCT_DIALOGUE_COORDINATOR_MAX 96u
#define LC_PRODUCT_DIALOGUE_COMPANION_MAX 144u
#define LC_PRODUCT_DIALOGUE_COMPANION_DELAY_MS 650u

#define LC_PRODUCT_DIALOGUE_COORDINATOR_VISIBLE (1u << 0)
#define LC_PRODUCT_DIALOGUE_COMPANION_VISIBLE (1u << 1)

typedef struct
{
  bool visible;
  char coordinator[LC_PRODUCT_DIALOGUE_COORDINATOR_MAX];
  char companion[LC_PRODUCT_DIALOGUE_COMPANION_MAX];
} lc_product_dialogue_t;

bool lc_product_dialogue_build(const lc_product_flow_t *flow,
                               lc_product_dialogue_t *dialogue);
uint8_t lc_product_dialogue_reveal_mask(
  const lc_product_dialogue_t *dialogue,
  uint32_t elapsed_ms);

#endif
