#ifndef LIVING_CANVAS_LC_GREETING_H
#define LIVING_CANVAS_LC_GREETING_H

#include <stdbool.h>
#include <stdint.h>

#define LC_GREETING_FRAME_COUNT 6u

typedef enum
{
  LC_GREETING_IDLE = 0,
  LC_GREETING_PLAYING,
  LC_GREETING_DONE,
  LC_GREETING_INVALID
} lc_greeting_status_t;

typedef struct
{
  bool active;
  uint32_t duration_ms;
  uint64_t started_ms;
} lc_greeting_animation_t;

typedef struct
{
  bool visible;
  unsigned int frame_index;
  int wave_offset_y;
  unsigned int scale_percent;
} lc_greeting_frame_t;

void lc_greeting_animation_init(lc_greeting_animation_t *animation,
                                uint32_t duration_ms);
void lc_greeting_animation_start(lc_greeting_animation_t *animation,
                                 uint64_t now_ms);
lc_greeting_status_t lc_greeting_animation_sample(
  lc_greeting_animation_t *animation,
  uint64_t now_ms,
  lc_greeting_frame_t *frame);

#endif
