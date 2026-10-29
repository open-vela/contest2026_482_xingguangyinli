#include "lc_greeting.h"

#include <assert.h>
#include <stdio.h>

static void test_animation_advances_and_finishes(void)
{
  lc_greeting_animation_t animation;
  lc_greeting_frame_t frame;

  lc_greeting_animation_init(&animation, 1200u);
  lc_greeting_animation_start(&animation, 1000u);
  assert(lc_greeting_animation_sample(&animation, 1000u, &frame) ==
         LC_GREETING_PLAYING);
  assert(frame.visible);
  assert(frame.frame_index == 0u);
  assert(frame.wave_offset_y == 0);

  assert(lc_greeting_animation_sample(&animation, 1400u, &frame) ==
         LC_GREETING_PLAYING);
  assert(frame.frame_index > 0u);
  assert(frame.wave_offset_y != 0);

  assert(lc_greeting_animation_sample(&animation, 2200u, &frame) ==
         LC_GREETING_DONE);
  assert(!frame.visible);
  assert(!animation.active);
}
static void test_clock_rollback_restarts_without_unsigned_jump(void)
{
  lc_greeting_animation_t animation;
  lc_greeting_frame_t frame;

  lc_greeting_animation_init(&animation, 1200u);
  lc_greeting_animation_start(&animation, 5000u);
  assert(lc_greeting_animation_sample(&animation, 100u, &frame) ==
         LC_GREETING_PLAYING);
  assert(frame.frame_index == 0u);
  assert(animation.started_ms == 100u);
}
static void test_invalid_and_idle_are_explicit(void)
{
  lc_greeting_animation_t animation;
  lc_greeting_frame_t frame;

  lc_greeting_animation_init(&animation, 0u);
  assert(lc_greeting_animation_sample(&animation, 0u, &frame) ==
         LC_GREETING_IDLE);
  assert(!frame.visible);
  assert(lc_greeting_animation_sample(NULL, 0u, &frame) ==
         LC_GREETING_INVALID);
  assert(lc_greeting_animation_sample(&animation, 0u, NULL) ==
         LC_GREETING_INVALID);
}

int main(void)
{
  test_animation_advances_and_finishes();
  test_clock_rollback_restarts_without_unsigned_jump();
  test_invalid_and_idle_are_explicit();
  puts("PASS: lc_greeting");
  return 0;
}
