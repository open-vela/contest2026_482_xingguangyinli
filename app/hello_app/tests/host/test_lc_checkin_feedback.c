#include "lc_checkin_feedback.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
  lc_checkin_feedback_t feedback;

  lc_checkin_feedback_init(&feedback, 2000u);
  assert(!lc_checkin_feedback_active(&feedback));
  assert(lc_checkin_feedback_on_accepted(&feedback, "cat_a", 100u));
  assert(lc_checkin_feedback_active(&feedback));
  assert(strcmp(lc_checkin_feedback_message(&feedback),
                "猫A已向画框报到") == 0);
  assert(lc_checkin_feedback_take_chime(&feedback) ==
         LC_CHECKIN_FEEDBACK_CAT_A);
  assert(lc_checkin_feedback_take_chime(&feedback) ==
         LC_CHECKIN_FEEDBACK_NONE);
  lc_checkin_feedback_update(&feedback, 2099u);
  assert(lc_checkin_feedback_active(&feedback));
  lc_checkin_feedback_update(&feedback, 2100u);
  assert(!lc_checkin_feedback_active(&feedback));

  assert(lc_checkin_feedback_on_accepted(&feedback, "cat_b", UINT32_MAX - 9u));
  assert(strcmp(lc_checkin_feedback_message(&feedback),
                "猫B已向画框报到") == 0);
  assert(lc_checkin_feedback_take_chime(&feedback) ==
         LC_CHECKIN_FEEDBACK_CAT_B);
  lc_checkin_feedback_update(&feedback, 1989u);
  assert(lc_checkin_feedback_active(&feedback));
  lc_checkin_feedback_update(&feedback, 1990u);
  assert(!lc_checkin_feedback_active(&feedback));
  assert(!lc_checkin_feedback_on_accepted(&feedback, "cat_c", 1u));

  puts("lc_checkin_feedback tests passed");
  return 0;
}
