#include "lc_product_dialogue.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

static bool set_bubbles(lc_product_dialogue_t *dialogue,
                        const char *coordinator_format,
                        const char *coordinator_value,
                        const char *companion_format,
                        const char *companion_value,
                        const char *companion_extra)
{
  int coordinator_length;
  int companion_length;

  coordinator_length = snprintf(dialogue->coordinator,
                                sizeof(dialogue->coordinator),
                                coordinator_format, coordinator_value);
  companion_length = snprintf(dialogue->companion,
                              sizeof(dialogue->companion),
                              companion_format, companion_value,
                              companion_extra);
  if (coordinator_length < 0 || companion_length < 0 ||
      (size_t)coordinator_length >= sizeof(dialogue->coordinator) ||
      (size_t)companion_length >= sizeof(dialogue->companion))
    {
      memset(dialogue, 0, sizeof(*dialogue));
      return false;
    }

  dialogue->visible = true;
  return true;
}

bool lc_product_dialogue_build(const lc_product_flow_t *flow,
                               lc_product_dialogue_t *dialogue)
{
  if (flow == NULL || dialogue == NULL)
    {
      return false;
    }

  memset(dialogue, 0, sizeof(*dialogue));
  switch (flow->state)
    {
      case LC_PRODUCT_CHOICE:
        return true;

      case LC_PRODUCT_REQUESTING:
        return set_bubbles(dialogue,
                           "比格犬：%s", "帮你选餐",
                           "猫咪：%s%s", "检查口味",
                           "");

      case LC_PRODUCT_RECOMMENDATION:
        return set_bubbles(dialogue,
                           "比格犬：%s", "请先确认",
                           "猫咪：%s%s", flow->result.name, "");

      case LC_PRODUCT_CONFIRMING:
        return set_bubbles(dialogue,
                           "比格犬：%s", "准备二维码",
                           "猫咪：%s%s", "到手机确认并付款", "");

      case LC_PRODUCT_QR:
        return set_bubbles(dialogue,
                           "比格犬：%s", "扫码打开饿了么",
                           "猫咪：%s%s", "由你在手机付款", "");

      case LC_PRODUCT_COMPLETE:
        return set_bubbles(dialogue,
                           "比格犬：%s", "已确认在家做饭",
                           "猫咪：%s%s", "没有提交外卖订单", "");

      case LC_PRODUCT_ERROR:
        if (flow->error == LC_PRODUCT_ERROR_NO_CANDIDATE)
          return set_bubbles(dialogue,
                             "比格犬：%s", "暂时没有合适的菜",
                             "猫咪：%s%s", "试着调整条件", "");
        return set_bubbles(dialogue,
                           "比格犬：%s", "服务暂未连通",
                           "猫咪：%s%s", "点击桌面物品重试", "");
    }

  return false;
}

uint8_t lc_product_dialogue_reveal_mask(
  const lc_product_dialogue_t *dialogue,
  uint32_t elapsed_ms)
{
  uint8_t mask;

  if (dialogue == NULL || !dialogue->visible)
    {
      return 0u;
    }

  mask = LC_PRODUCT_DIALOGUE_COORDINATOR_VISIBLE;
  if (elapsed_ms >= LC_PRODUCT_DIALOGUE_COMPANION_DELAY_MS)
    {
      mask |= LC_PRODUCT_DIALOGUE_COMPANION_VISIBLE;
    }

  return mask;
}
