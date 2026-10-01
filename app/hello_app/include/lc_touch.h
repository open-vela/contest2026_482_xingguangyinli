#ifndef LC_TOUCH_H
#define LC_TOUCH_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  int32_t x;
  int32_t y;
} lc_touch_point_t;

lc_touch_point_t lc_touch_transform(int32_t raw_x, int32_t raw_y,
                                    int32_t width, int32_t height,
                                    bool swap_xy);

#endif
