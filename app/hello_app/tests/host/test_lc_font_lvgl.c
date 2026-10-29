/* Run against the real SDK LVGL library, not the host UI stubs. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lvgl/lvgl.h>

#ifdef LC_TEST_CJK_FONT
LV_FONT_DECLARE(lc_font_cjk_14);
#define TEST_FONT (&lc_font_cjk_14)
#else
#define TEST_FONT LV_FONT_DEFAULT
#endif

static unsigned int flush_count;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
  FILE *file = fopen("font-preview.ppm", "wb");
  assert(file != NULL);
  assert(area->x1 == 0 && area->y1 == 0);
  assert(area->x2 == 239 && area->y2 == 319);
  fprintf(file, "P6\n240 320\n255\n");
  /* LVGL RGB888 uses B,G,R byte order. */
  for (unsigned int i = 0; i < 240u * 320u; i++)
    {
      uint8_t rgb[3] = {pixels[i * 3u + 2u], pixels[i * 3u + 1u],
                        pixels[i * 3u]};
      assert(fwrite(rgb, 1, sizeof(rgb), file) == sizeof(rgb));
    }
  assert(fclose(file) == 0);
  flush_count++;
  lv_display_flush_ready(display);
}

int main(void)
{
  /* Simplified/traditional names and required UI punctuation. */
  const uint32_t required[] = {0x756a, 0x8304, 0x725b, 0x8089, 0x996d,
    0x9e21, 0x817f, 0x997a, 0x5b50, 0x9ebb, 0x8fa3, 0x9999, 0x83dc,
    0x78ba, 0x8ba4, 0xffe5, 0x00a5, 0xff0c, 0x3002, 0x2026};
  lv_font_glyph_dsc_t glyph;
  unsigned int covered = 0;
  lv_init();
  for (unsigned int i = 0; i < sizeof(required) / sizeof(required[0]); i++)
    {
      if (!lv_font_get_glyph_dsc(TEST_FONT, &glyph, required[i], 0) ||
          glyph.is_placeholder)
        {
          fprintf(stderr, "missing real glyph: U+%04X\n", required[i]);
          return EXIT_FAILURE;
        }
      assert(glyph.box_w > 0 && glyph.box_h > 0);
    }
  for (uint32_t code = 0x4e00; code <= 0x9fff; code++)
    if (lv_font_get_glyph_dsc(TEST_FONT, &glyph, code, 0) &&
        !glyph.is_placeholder)
      covered++;
  assert(covered >= 20900); /* Not a tiny hand-picked dish-name subset. */
  lv_display_t *display = lv_display_create(240, 320);
  assert(display != NULL);
  static uint8_t buffer[240 * 320 * 3];
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
  lv_display_set_buffers(display, buffer, NULL, sizeof(buffer),
                         LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, flush);
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0xfff7e8), 0);
  lv_obj_t *label = lv_label_create(screen);
  lv_obj_set_style_text_font(label, TEST_FONT, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0x482514), 0);
  lv_obj_set_width(label, 210);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 20);
  lv_label_set_text(label, "中文菜名显示测试\n\n番茄牛肉饭\n照烧鸡腿饭\n麻辣饺子\n不吃香菜\n预算 ￥25–40\n\n猫咪：换一道？\n狗狗：确认后扫码");
  lv_refr_now(display);
  assert(flush_count > 0);
  printf("PASS: real LVGL CJK glyphs: %u; rendered font-preview.ppm\n", covered);
  lv_deinit();
  return EXIT_SUCCESS;
}
