#include "lc_room_view.h"
#include "generated/lc_choice_assets.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(lc_font_cjk_14);
static uint8_t pixels[240 * 320 * 3];
static uint8_t idle[sizeof(pixels)];
static const char *output;
static unsigned int clicks;

static void clicked(lv_event_t *event)
{
  assert(lv_event_get_user_data(event) == &clicks);
  clicks++;
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
  assert(area->x1 == 0 && area->y1 == 0 && area->x2 == 239 && area->y2 == 319);
  memcpy(pixels, data, sizeof(pixels));
  FILE *file = fopen(output, "wb");
  assert(file != NULL);
  fprintf(file, "P6\n240 320\n255\n");
  for (size_t i = 0u; i < 240u * 320u; i++)
    {
      uint8_t rgb[3] = {data[3u * i + 2u], data[3u * i + 1u], data[3u * i]};
      assert(fwrite(rgb, 1u, 3u, file) == 3u);
    }
  assert(fclose(file) == 0);
  lv_display_flush_ready(display);
}

int main(void)
{
  lv_init();
  lv_display_t *display = lv_display_create(240, 320);
  assert(display != NULL);
  static uint8_t buffer[sizeof(pixels)];
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
  lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, flush);
  lc_ui_room_layout_t room;
  assert(lc_ui_compute_room_layout(240, 320, &room));
  lv_obj_t *screen = lv_screen_active();
  static const lv_image_dsc_t bg = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
               .w = 240, .h = 320, .stride = 480},
    .data_size = LC_BG_PORTRAIT_BYTES, .data = lc_bg_portrait
  };
  lv_obj_t *image = lv_image_create(screen);
  lv_image_set_src(image, &bg);
  lv_obj_set_pos(image, room.artwork.x, room.artwork.y);
  static const lv_image_dsc_t source[2] = {
    {.header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888,
                .w = 96, .h = 40, .stride = 384},
      .data_size = LC_BUBBLE_BYTES, .data = lc_bubble_cat},
    {.header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888,
                .w = 96, .h = 40, .stride = 384},
      .data_size = LC_BUBBLE_BYTES, .data = lc_bubble_dog}
  };
  lv_obj_t *hotspots[3];
  for (unsigned int i = 0u; i < 3u; i++)
    {
      hotspots[i] = lc_room_hotspot_create(screen, &room.objects[i], clicked, &clicks);
      assert(lv_obj_get_style_bg_opa(hotspots[i], 0) == LV_OPA_TRANSP);
      assert(lv_obj_get_style_border_width(hotspots[i], 0) == 0);
      assert(!lv_obj_has_flag(hotspots[i], LV_OBJ_FLAG_SCROLLABLE));
    }
  lv_obj_t *labels[2];
  lv_obj_t *bubbles[2];
  bubbles[0] = lc_room_bubble_create(screen, &room.cat_bubble, &source[0], room.scale,
    &lc_font_cjk_14, &labels[0], clicked, &clicks);
  bubbles[1] = lc_room_bubble_create(screen, &room.dog_bubble, &source[1], room.scale,
    &lc_font_cjk_14, &labels[1], clicked, &clicks);
  output = "room-idle.ppm";
  lv_refr_now(display);
  memcpy(idle, pixels, sizeof(idle));
  lv_obj_set_style_border_width(hotspots[0], 2, 0);
  lv_obj_set_style_border_color(hotspots[0], lv_color_hex(0xffd66b), 0);
  output = "room-selected.ppm";
  lv_refr_now(display);
  assert(memcmp(idle, pixels, sizeof(idle)) != 0);
  lv_obj_set_style_border_width(hotspots[0], 0, 0);
  lv_label_set_text(labels[0], "照烧鸡腿饭");
  lv_label_set_text(labels[1], "请先确认");
  lv_obj_remove_flag(bubbles[0], LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(bubbles[1], LV_OBJ_FLAG_HIDDEN);
  output = "room-dialogue.ppm";
  lv_refr_now(display);
  /* Overlay creation must leave the complete face/body band unchanged. */
  for (unsigned int y = 119u; y < 186u; y++)
    assert(memcmp(idle + y * 240u * 3u, pixels + y * 240u * 3u, 240u * 3u) == 0);
  assert(lv_obj_get_style_text_font(labels[0], 0) == &lc_font_cjk_14);
  assert(lv_obj_get_style_border_width(hotspots[1], 0) == 0);
  lv_obj_send_event(hotspots[0], LV_EVENT_CLICKED, NULL);
  lv_obj_send_event(bubbles[1], LV_EVENT_CLICKED, NULL);
  assert(clicks == 2u);
  lv_obj_add_flag(bubbles[0], LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(bubbles[1], LV_OBJ_FLAG_HIDDEN);
  lv_obj_t *greeting_label;
  lv_obj_t *greeting = lc_room_greeting_create(screen, &room, &source[1],
    &lc_font_cjk_14, &greeting_label);
  assert(lv_obj_has_flag(greeting, LV_OBJ_FLAG_HIDDEN));
  assert(!lv_obj_has_flag(greeting, LV_OBJ_FLAG_CLICKABLE));
  assert(lv_obj_get_style_bg_opa(greeting, 0) == LV_OPA_TRANSP);
  assert(strcmp(lv_label_get_text(greeting_label), "你回来啦！") == 0);
  lv_obj_remove_flag(greeting, LV_OBJ_FLAG_HIDDEN);
  output = "room-greeting.ppm";
  lv_refr_now(display);
  assert(memcmp(idle, pixels, sizeof(idle)) != 0);
  for (unsigned int y = 119u; y < 186u; y++)
    assert(memcmp(idle + y * 240u * 3u, pixels + y * 240u * 3u, 240u * 3u) == 0);
  lv_obj_add_flag(greeting, LV_OBJ_FLAG_HIDDEN);
  output = "room-after-greeting.ppm";
  lv_refr_now(display);
  assert(memcmp(idle, pixels, sizeof(idle)) == 0);
  lv_deinit();
  puts("PASS: real shared LVGL room nodes, transparency, clicks and face-region pixels");
  return 0;
}
