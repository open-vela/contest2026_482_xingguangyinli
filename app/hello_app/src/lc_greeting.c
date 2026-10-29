#include "lc_greeting.h"

#include <stddef.h>
#include <string.h>

#define LC_GREETING_DEFAULT_DURATION_MS 1200u

void lc_greeting_animation_init(lc_greeting_animation_t *animation,
                                uint32_t duration_ms)
{
  if (animation == NULL)
    {
      return;
    }

  memset(animation, 0, sizeof(*animation));
  animation->duration_ms = duration_ms == 0u
                             ? LC_GREETING_DEFAULT_DURATION_MS
                             : duration_ms;
}
void lc_greeting_animation_start(lc_greeting_animation_t *animation,
                                 uint64_t now_ms)
{
  if (animation == NULL)
    {
      return;
    }

  animation->active = true;
  animation->started_ms = now_ms;
}
lc_greeting_status_t lc_greeting_animation_sample(
  lc_greeting_animation_t *animation,
  uint64_t now_ms,
  lc_greeting_frame_t *frame)
{
  uint64_t elapsed;
  uint32_t slot_ms;
  unsigned int frame_index;

  if (animation == NULL || frame == NULL || animation->duration_ms == 0u)
    {
      return LC_GREETING_INVALID;
    }

  memset(frame, 0, sizeof(*frame));
  frame->scale_percent = 100u;
  if (!animation->active)
    {
      return LC_GREETING_IDLE;
    }

  if (now_ms < animation->started_ms)
    {
      animation->started_ms = now_ms;
    }

  elapsed = now_ms - animation->started_ms;
  if (elapsed >= animation->duration_ms)
    {
      animation->active = false;
      return LC_GREETING_DONE;
    }

  slot_ms = animation->duration_ms / LC_GREETING_FRAME_COUNT;
  if (slot_ms == 0u)
    {
      slot_ms = 1u;
    }

  frame_index = (unsigned int)(elapsed / slot_ms);
  if (frame_index >= LC_GREETING_FRAME_COUNT)
    {
      frame_index = LC_GREETING_FRAME_COUNT - 1u;
    }

  frame->visible = true;
  frame->frame_index = frame_index;
  frame->wave_offset_y = frame_index == 0u
                           ? 0 : (frame_index % 2u == 0u ? 3 : -3);
  frame->scale_percent = 100u +
                         (frame_index == 1u || frame_index == 4u ? 4u : 0u);
  return LC_GREETING_PLAYING;
}
