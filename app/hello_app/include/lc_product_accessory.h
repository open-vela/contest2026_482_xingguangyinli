#ifndef LIVING_CANVAS_LC_PRODUCT_ACCESSORY_H
#define LIVING_CANVAS_LC_PRODUCT_ACCESSORY_H

#include "lc_accessory_runtime.h"
#include "lc_product_flow.h"

typedef enum
{
  LC_PRODUCT_ACCESSORY_NONE = 0,
  LC_PRODUCT_ACCESSORY_CONFIRM,
  LC_PRODUCT_ACCESSORY_CANCEL
} lc_product_accessory_action_t;

lc_product_accessory_action_t lc_product_accessory_decide(
  lc_presence_source_t source,
  const lc_accessory_runtime_result_t *result,
  const lc_product_flow_t *flow);

#endif
