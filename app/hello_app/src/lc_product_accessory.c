#include "lc_product_accessory.h"

lc_product_accessory_action_t lc_product_accessory_decide(
  lc_presence_source_t source,
  const lc_accessory_runtime_result_t *result,
  const lc_product_flow_t *flow)
{
  if (source != LC_PRESENCE_SOURCE_ACCESSORY || result == NULL ||
      flow == NULL || result->status != LC_ACCESSORY_RUNTIME_ACCEPTED ||
      (result->acceptance != LC_ACCESSORY_ACCEPTED &&
       result->acceptance != LC_ACCESSORY_NEW_SESSION) ||
      result->event.value != 0)
    {
      return LC_PRODUCT_ACCESSORY_NONE;
    }

  if (result->event.type == LC_ACCESSORY_CONFIRM &&
      flow->state == LC_PRODUCT_RECOMMENDATION)
    {
      return LC_PRODUCT_ACCESSORY_CONFIRM;
    }

  if (result->event.type == LC_ACCESSORY_CANCEL &&
      (flow->state == LC_PRODUCT_RECOMMENDATION ||
       (flow->state == LC_PRODUCT_ERROR && flow->retryable)))
    {
      return LC_PRODUCT_ACCESSORY_CANCEL;
    }

  return LC_PRODUCT_ACCESSORY_NONE;
}
