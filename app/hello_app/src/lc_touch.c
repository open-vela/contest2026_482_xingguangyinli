#include "lc_touch.h"

static int32_t clamp_coordinate(int32_t value, int32_t extent)
{
  if (value < 0 || extent <= 0)
    {
      return 0;
    }

  if (value >= extent)
    {
      return extent - 1;
    }

  return value;
}

lc_touch_point_t lc_touch_transform(int32_t raw_x, int32_t raw_y,
                                    int32_t width, int32_t height,
                                    bool swap_xy)
{
  lc_touch_point_t point;
  int32_t x = swap_xy ? raw_y : raw_x;
  int32_t y = swap_xy ? raw_x : raw_y;

  point.x = clamp_coordinate(x, width);
  point.y = clamp_coordinate(y, height);
  return point;
}
