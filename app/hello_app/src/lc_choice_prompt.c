#include "lc_choice_prompt.h"

#include <string.h>

static const char *prompt_for_choice(lc_choice_id_t choice)
{
  switch (choice)
    {
      case LC_CHOICE_TAKEOUT:
        return "你是画间晚餐助手。用户选择了外卖。请结合清淡、预算和可执行性，给出一个具体晚餐建议；只用一句简短中文，不要声称已经下单。";

      case LC_CHOICE_MYSTERY:
        return "你是画间晚餐助手。用户选择了盲盒。请给出一个有惊喜但稳妥的具体晚餐建议；只用一句简短中文，不要声称已经下单。";

      case LC_CHOICE_HOME:
        return "你是画间晚餐助手。用户选择了在家吃。请给出一个食材常见、步骤简单的具体晚餐建议；只用一句简短中文。";

      case LC_CHOICE_COUNT:
        break;
    }

  return NULL;
}

bool lc_choice_build_prompt(lc_choice_id_t choice,
                            char *prompt,
                            size_t prompt_capacity)
{
  const char *source = prompt_for_choice(choice);
  size_t length;

  if (source == NULL || prompt == NULL)
    {
      return false;
    }

  length = strlen(source);
  if (length + 1u > prompt_capacity || length + 1u > LC_CHOICE_PROMPT_MAX)
    {
      return false;
    }

  memcpy(prompt, source, length + 1u);
  return true;
}
