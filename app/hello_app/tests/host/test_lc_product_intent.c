#include "lc_product_intent.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
  lc_choice_id_t choice = LC_CHOICE_COUNT;

  assert(lc_product_intent_classify("我今天想点个外卖", &choice));
  assert(choice == LC_CHOICE_TAKEOUT);

  assert(lc_product_intent_classify("随便来个惊喜盲盒", &choice));
  assert(choice == LC_CHOICE_MYSTERY);

  assert(lc_product_intent_classify("今晚在家做饭", &choice));
  assert(choice == LC_CHOICE_HOME);

  assert(lc_product_intent_classify("takeout please", &choice));
  assert(choice == LC_CHOICE_TAKEOUT);

  assert(!lc_product_intent_classify("我还没有想好", &choice));
  assert(!lc_product_intent_classify(NULL, &choice));
  assert(!lc_product_intent_classify("", NULL));

  assert(lc_product_intent_route("一人吃饭，预算四十元，微辣，不要香菜", LC_CHOICE_TAKEOUT,
    false, &choice) == LC_PRODUCT_VOICE_REQUEST && choice == LC_CHOICE_TAKEOUT);
  assert(lc_product_intent_route("鸡蛋番茄，想吃清淡一点", LC_CHOICE_HOME,
    false, &choice) == LC_PRODUCT_VOICE_REQUEST && choice == LC_CHOICE_HOME);
  assert(lc_product_intent_route("我不爱吃这个，换一个", LC_CHOICE_TAKEOUT,
    true, &choice) == LC_PRODUCT_VOICE_REJECT && choice == LC_CHOICE_TAKEOUT);
  assert(lc_product_intent_route("确认。", LC_CHOICE_TAKEOUT,
    true, &choice) == LC_PRODUCT_VOICE_CONFIRM);
  assert(lc_product_intent_route("就这个！", LC_CHOICE_TAKEOUT,
    true, &choice) == LC_PRODUCT_VOICE_CONFIRM);
  assert(lc_product_intent_route(" 确认 ", LC_CHOICE_HOME,
    true, &choice) == LC_PRODUCT_VOICE_CONFIRM);
  const char *uncertain[] = {"确认吗", "不要确认", "不能确认", "确认但是不下单", "可以", "嗯", "不知道"};
  for (size_t i = 0; i < sizeof(uncertain)/sizeof(uncertain[0]); i++)
    assert(lc_product_intent_route(uncertain[i], LC_CHOICE_TAKEOUT,
      true, &choice) != LC_PRODUCT_VOICE_CONFIRM);
  assert(lc_product_intent_route("确认", LC_CHOICE_TAKEOUT,
    false, &choice) == LC_PRODUCT_VOICE_UNRESOLVED);
  assert(lc_product_intent_route("我今天想在家做饭", LC_CHOICE_TAKEOUT,
    true, &choice) == LC_PRODUCT_VOICE_REQUEST && choice == LC_CHOICE_HOME);
  assert(lc_product_intent_route("我想吃热乎的饭", LC_CHOICE_COUNT,
    false, &choice) == LC_PRODUCT_VOICE_REQUEST && choice == LC_CHOICE_TAKEOUT);
  assert(lc_product_intent_route(NULL, LC_CHOICE_TAKEOUT,
    true, &choice) == LC_PRODUCT_VOICE_UNRESOLVED);

  puts("PASS: lc_product_intent");
  return 0;
}
