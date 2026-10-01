#include "lc_touch.h"

#include <assert.h>
#include <stdio.h>

static void test_portrait_swaps_axes(void)
{
  lc_touch_point_t point = lc_touch_transform(190, 108, 240, 320, true);

  assert(point.x == 108);
  assert(point.y == 190);
}

static void test_portrait_preserves_bottom_row_after_swap(void)
{
  lc_touch_point_t point = lc_touch_transform(300, 120, 240, 320, true);

  assert(point.x == 120);
  assert(point.y == 300);
}

static void test_coordinates_are_clamped_after_transform(void)
{
  lc_touch_point_t point = lc_touch_transform(400, 260, 240, 320, true);

  assert(point.x == 239);
  assert(point.y == 319);
}

static void test_landscape_keeps_native_axes(void)
{
  lc_touch_point_t point = lc_touch_transform(512, 300, 1024, 600, false);

  assert(point.x == 512);
  assert(point.y == 300);
}

int main(void)
{
  test_portrait_swaps_axes();
  test_portrait_preserves_bottom_row_after_swap();
  test_coordinates_are_clamped_after_transform();
  test_landscape_keeps_native_axes();
  puts("PASS: lc_touch");
  return 0;
}
