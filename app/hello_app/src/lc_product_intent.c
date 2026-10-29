#include "lc_product_intent.h"

#include <stddef.h>
#include <string.h>

static bool contains_any(const char *text,
                         const char *const terms[],
                         size_t term_count)
{
  size_t index;

  for (index = 0u; index < term_count; index++)
    {
      if (strstr(text, terms[index]) != NULL)
        {
          return true;
        }
    }

  return false;
}

bool lc_product_intent_classify(const char *transcript,
                                lc_choice_id_t *choice)
{
  static const char *const takeout_terms[] =
  {
    "外卖", "点餐", "takeout", "delivery"
  };
  static const char *const mystery_terms[] =
  {
    "盲盒", "惊喜", "随便", "mystery", "surprise"
  };
  static const char *const home_terms[] =
  {
    "在家", "做饭", "下厨", "home", "cook"
  };

  if (transcript == NULL || transcript[0] == '\0' || choice == NULL)
    {
      return false;
    }

  if (contains_any(transcript, takeout_terms,
                   sizeof(takeout_terms) / sizeof(takeout_terms[0])))
    {
      *choice = LC_CHOICE_TAKEOUT;
      return true;
    }

  if (contains_any(transcript, mystery_terms,
                   sizeof(mystery_terms) / sizeof(mystery_terms[0])))
    {
      *choice = LC_CHOICE_MYSTERY;
      return true;
    }

  if (contains_any(transcript, home_terms,
                   sizeof(home_terms) / sizeof(home_terms[0])))
    {
      *choice = LC_CHOICE_HOME;
      return true;
    }

  return false;
}

static bool explicit_confirmation(const char *text)
{
  static const char *const phrases[] =
    {"确认", "确定", "就这个", "选这个", "就吃这个", "我要这个"};
  /* Only spaces and terminal punctuation are ignorable. Preserve question
   * marks/negation/extra clauses so ASR uncertainty cannot authorize handoff. */
  while (*text == ' ' || *text == '\t') ++text;
  size_t length = strlen(text);
  for (;;)
    {
      if (length > 0u && (text[length - 1u] == ' ' || text[length - 1u] == '\t' ||
                         text[length - 1u] == '.' || text[length - 1u] == '!'))
        { --length; continue; }
      if (length >= 3u && (memcmp(text + length - 3u, "。", 3u) == 0 ||
                           memcmp(text + length - 3u, "！", 3u) == 0))
        { length -= 3u; continue; }
      break;
    }
  for (size_t i = 0; i < sizeof(phrases) / sizeof(phrases[0]); ++i)
    if (length == strlen(phrases[i]) && memcmp(text, phrases[i], length) == 0)
      return true;
  return false;
}

lc_product_voice_action_t lc_product_intent_route(const char *transcript,
  lc_choice_id_t selected, bool has_candidate, lc_choice_id_t *choice)
{
  static const char *const refusal[] = {"不爱吃", "不想吃", "不喜欢", "不要这个",
    "不吃这个", "换一个", "换一道", "换下一个", "别选这个"};
  static const char *const requirements[] = {"吃", "辣", "饭", "面", "预算", "元",
    "块", "分钟", "半小时", "忌口", "过敏", "清淡", "鸡蛋", "番茄", "食材",
    "一个人", "两个人"};
  if (choice == NULL || transcript == NULL || transcript[0] == '\0')
    return LC_PRODUCT_VOICE_UNRESOLVED;
  *choice = selected < LC_CHOICE_COUNT && selected >= LC_CHOICE_TAKEOUT
    ? selected : LC_CHOICE_TAKEOUT;
  if (has_candidate && contains_any(transcript, refusal,
        sizeof(refusal) / sizeof(refusal[0]))) return LC_PRODUCT_VOICE_REJECT;
  if (explicit_confirmation(transcript))
    return has_candidate ? LC_PRODUCT_VOICE_CONFIRM : LC_PRODUCT_VOICE_UNRESOLVED;
  if (lc_product_intent_classify(transcript, choice)) return LC_PRODUCT_VOICE_REQUEST;
  if (contains_any(transcript, requirements, sizeof(requirements) / sizeof(requirements[0])))
    return LC_PRODUCT_VOICE_REQUEST;
  return LC_PRODUCT_VOICE_UNRESOLVED;
}
