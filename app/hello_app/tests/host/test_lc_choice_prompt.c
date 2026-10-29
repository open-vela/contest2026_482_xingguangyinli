#include "lc_choice_prompt.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
  char prompt[LC_CHOICE_PROMPT_MAX];

  assert(!lc_choice_build_prompt(LC_CHOICE_COUNT, prompt, sizeof(prompt)));
  assert(!lc_choice_build_prompt(LC_CHOICE_TAKEOUT, NULL, sizeof(prompt)));
  assert(!lc_choice_build_prompt(LC_CHOICE_TAKEOUT, prompt, 8u));

  assert(lc_choice_build_prompt(LC_CHOICE_TAKEOUT, prompt,
                                sizeof(prompt)));
  assert(strstr(prompt, "外卖") != NULL);
  assert(strstr(prompt, "一句") != NULL);

  assert(lc_choice_build_prompt(LC_CHOICE_MYSTERY, prompt,
                                sizeof(prompt)));
  assert(strstr(prompt, "盲盒") != NULL);

  assert(lc_choice_build_prompt(LC_CHOICE_HOME, prompt, sizeof(prompt)));
  assert(strstr(prompt, "在家") != NULL);

  puts("PASS: lc_choice_prompt");
  return 0;
}
