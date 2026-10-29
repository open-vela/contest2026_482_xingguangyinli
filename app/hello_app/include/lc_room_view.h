#ifndef LC_ROOM_VIEW_H
#define LC_ROOM_VIEW_H
#include "lc_ui.h"
#include <lvgl/lvgl.h>

/* Shared by device display and real LVGL visual tests. Source/font must
 * outlive their view. Creation starts with no selection and hidden bubbles. */
lv_obj_t *lc_room_hotspot_create(lv_obj_t *screen, const lc_ui_rect_t *rect,
                                lv_event_cb_t callback, void *user_data);
lv_obj_t *lc_room_bubble_create(lv_obj_t *screen, const lc_ui_rect_t *rect,
  const lv_image_dsc_t *source, unsigned int scale, const lv_font_t *font,
  lv_obj_t **label, lv_event_cb_t callback, void *user_data);
/* Non-interactive, initially hidden greeting above the dog's head. */
lv_obj_t *lc_room_greeting_create(lv_obj_t *screen,
  const lc_ui_room_layout_t *room, const lv_image_dsc_t *source,
  const lv_font_t *font, lv_obj_t **label);
#endif
