#include "lc_ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_idle_scene_is_quiet(void)
{
  lc_ui_model_t model;

  assert(lc_ui_build_model(LC_STATE_IDLE, true, 6u, 30u, &model) ==
         LC_UI_OK);
  assert(!model.cat_eyes_open);
  assert(!model.qr_visible);
  assert(model.clock_visible);
  assert(strcmp(model.state_label, "IDLE") == 0);
  assert(strcmp(model.time_label, "06:30") == 0);
  assert(model.clock_angles.hour_tenths == 1950u);
}

static void test_interaction_states_open_eyes(void)
{
  const lc_state_t states[] = {
    LC_STATE_GREETING,
    LC_STATE_LISTENING,
    LC_STATE_THINKING,
    LC_STATE_RECOMMENDATION,
    LC_STATE_CONFIRMED,
    LC_STATE_ERROR,
  };
  size_t index;

  for (index = 0u; index < sizeof(states) / sizeof(states[0]); index++)
    {
      lc_ui_model_t model;
      assert(lc_ui_build_model(states[index], true, 12u, 0u, &model) ==
             LC_UI_OK);
      assert(model.cat_eyes_open);
    }
}

static void test_qr_is_visible_only_after_confirmation(void)
{
  lc_ui_model_t model;

  lc_ui_build_model(LC_STATE_RECOMMENDATION, true, 12u, 0u, &model);
  assert(!model.qr_visible);
  lc_ui_build_model(LC_STATE_CONFIRMED, true, 12u, 0u, &model);
  assert(model.qr_visible);
}

static void test_unsynced_time_uses_explicit_placeholder(void)
{
  lc_ui_model_t model;

  assert(lc_ui_build_model(LC_STATE_IDLE, false, 18u, 45u, &model) ==
         LC_UI_TIME_UNSYNCED);
  assert(!model.clock_visible);
  assert(strcmp(model.time_label, "--:--") == 0);
}

static void test_invalid_input_is_rejected(void)
{
  lc_ui_model_t model;

  assert(lc_ui_build_model((lc_state_t)99, true, 12u, 0u, &model) ==
         LC_UI_INVALID);
  assert(lc_ui_build_model(LC_STATE_IDLE, true, 25u, 0u, &model) ==
         LC_UI_INVALID);
  assert(lc_ui_build_model(LC_STATE_IDLE, true, 12u, 0u, NULL) ==
         LC_UI_INVALID);
}

static bool rect_within(lc_ui_rect_t rect,
                        unsigned int width,
                        unsigned int height)
{
  return rect.width > 0u && rect.height > 0u && rect.x < width &&
         rect.y < height && rect.width <= width - rect.x &&
         rect.height <= height - rect.y;
}

static void test_relative_layout_stays_within_screen(void)
{
  const unsigned int dimensions[][2] = {
    {320u, 240u},
    {240u, 320u},
    {1280u, 720u},
  };
  size_t index;

  for (index = 0u; index < sizeof(dimensions) / sizeof(dimensions[0]);
       index++)
    {
      unsigned int width = dimensions[index][0];
      unsigned int height = dimensions[index][1];
      lc_ui_layout_t layout;

      assert(lc_ui_compute_layout(width, height, &layout));
      assert(rect_within(layout.clock, width, height));
      assert(rect_within(layout.weather, width, height));
      assert(rect_within(layout.cat, width, height));
      assert(rect_within(layout.dialog, width, height));
      assert(rect_within(layout.qr, width, height));
      assert(layout.clock.y == layout.weather.y);
      assert(layout.cat.y >= layout.clock.y + layout.clock.height);
      assert(layout.dialog.y >= layout.cat.y + layout.cat.height);
      assert(layout.qr.x >= layout.dialog.x);
      assert(layout.qr.y >= layout.dialog.y);
      assert(layout.qr.x + layout.qr.width <=
             layout.dialog.x + layout.dialog.width);
      assert(layout.qr.y + layout.qr.height <=
             layout.dialog.y + layout.dialog.height);
    }
}

static void test_layout_rejects_unusable_dimensions(void)
{
  lc_ui_layout_t layout;

  assert(!lc_ui_compute_layout(0u, 240u, &layout));
  assert(!lc_ui_compute_layout(320u, 0u, &layout));
  assert(!lc_ui_compute_layout(63u, 63u, &layout));
  assert(!lc_ui_compute_layout(320u, 240u, NULL));
}

static void test_choice_layout_preserves_small_panel_geometry(void)
{
  lc_ui_choice_layout_t layout;

  assert(lc_ui_compute_choice_layout(240u, 320u, &layout));
  assert(layout.background_width == 240u);
  assert(layout.background_height == 320u);
  assert(layout.background_scale == 256u);
  assert(layout.card_size == 70u);
  assert(layout.card_spacing == 76u);
  assert(layout.card_bottom == 5u);
  assert(layout.rotate_touch);
}

static void test_choice_layout_fits_seven_inch_landscape(void)
{
  lc_ui_choice_layout_t layout;

  assert(lc_ui_compute_choice_layout(1024u, 600u, &layout));
  assert(layout.background_width == 320u);
  assert(layout.background_height == 240u);
  assert(layout.background_scale == 640u);
  assert(layout.card_size >= 96u);
  assert(layout.card_spacing > layout.card_size);
  assert(layout.card_bottom >= 12u);
  assert(!layout.rotate_touch);
}

static void test_choice_layout_rejects_unusable_dimensions(void)
{
  lc_ui_choice_layout_t layout;

  assert(!lc_ui_compute_choice_layout(63u, 600u, &layout));
  assert(!lc_ui_compute_choice_layout(1024u, 63u, &layout));
  assert(!lc_ui_compute_choice_layout(1024u, 600u, NULL));
}

static void test_room_objects_match_supplied_artwork(void)
{
  lc_ui_room_layout_t room;
  assert(lc_ui_compute_room_layout(240u, 320u, &room));
  assert(room.objects[0].x == 54u && room.objects[0].y == 186u);
  assert(room.objects[1].x == 101u && room.objects[1].y == 204u);
  assert(room.objects[2].x == 147u && room.objects[2].y == 210u);
  assert(room.objects[0].width >= 44u);
  assert(room.objects[0].x + room.objects[0].width <= room.objects[1].x);
  assert(room.objects[1].x + room.objects[1].width <= room.objects[2].x);
  assert(room.cat_bubble.y + room.cat_bubble.height <= 119u);
  assert(room.dog_bubble.y + room.dog_bubble.height <= 114u);
  assert(room.result.y >= 250u);
}

static void test_room_geometry_stays_inside_letterboxed_artwork(void)
{
  const unsigned int sizes[][2] = {{240u,320u}, {320u,240u}, {1024u,600u}, {64u,64u}};
  size_t i;
  for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    {
      lc_ui_room_layout_t room;
      unsigned int j;
      assert(lc_ui_compute_room_layout(sizes[i][0], sizes[i][1], &room));
      assert(rect_within(room.artwork, sizes[i][0], sizes[i][1]));
      assert(rect_within(room.cat_bubble, sizes[i][0], sizes[i][1]));
      assert(rect_within(room.dog_bubble, sizes[i][0], sizes[i][1]));
      assert(rect_within(room.result, sizes[i][0], sizes[i][1]));
      for (j = 0; j < 3u; j++)
        {
          assert(rect_within(room.objects[j], sizes[i][0], sizes[i][1]));
          assert(room.objects[j].x >= room.artwork.x);
          assert(room.objects[j].x + room.objects[j].width <= room.artwork.x + room.artwork.width);
        }
    }
  assert(!lc_ui_compute_room_layout(63u, 320u, NULL));
  assert(!lc_ui_compute_room_layout(240u, 320u, NULL));
}

int main(void)
{
  test_idle_scene_is_quiet();
  test_interaction_states_open_eyes();
  test_qr_is_visible_only_after_confirmation();
  test_unsynced_time_uses_explicit_placeholder();
  test_invalid_input_is_rejected();
  test_relative_layout_stays_within_screen();
  test_layout_rejects_unusable_dimensions();
  test_choice_layout_preserves_small_panel_geometry();
  test_choice_layout_fits_seven_inch_landscape();
  test_choice_layout_rejects_unusable_dimensions();
  test_room_objects_match_supplied_artwork();
  test_room_geometry_stays_inside_letterboxed_artwork();
  puts("PASS: lc_ui");
  return 0;
}
