#include "lc_room_view.h"

static lv_obj_t *room_box(lv_obj_t *screen, const lc_ui_rect_t *rect,
                          lv_event_cb_t callback, void *user_data)
{
  lv_obj_t *box = lv_obj_create(screen);
  lv_obj_remove_style_all(box);
  lv_obj_set_pos(box, rect->x, rect->y);
  lv_obj_set_size(box, rect->width, rect->height);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  if (callback != NULL)
    {
      lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(box, callback, LV_EVENT_CLICKED, user_data);
    }
  else lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
  return box;
}

lv_obj_t *lc_room_hotspot_create(lv_obj_t *screen, const lc_ui_rect_t *rect,
                                lv_event_cb_t callback, void *user_data)
{
  lv_obj_t *hotspot = room_box(screen, rect, callback, user_data);
  lv_obj_set_style_radius(hotspot, 6, 0);
  lv_obj_set_style_border_width(hotspot, 0, 0);
  return hotspot;
}

lv_obj_t *lc_room_bubble_create(lv_obj_t *screen, const lc_ui_rect_t *rect,
  const lv_image_dsc_t *source, unsigned int scale, const lv_font_t *font,
  lv_obj_t **label, lv_event_cb_t callback, void *user_data)
{
  lv_obj_t *bubble = room_box(screen, rect, callback, user_data);
  lv_obj_t *image = lv_image_create(bubble);
  lv_image_set_src(image, source);
  lv_image_set_scale(image, scale);
  lv_obj_center(image);
  lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
  *label = lv_label_create(bubble);
  lv_obj_set_style_text_align(*label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(*label, lv_color_hex(0x482514), 0);
  lv_obj_set_style_text_font(*label, font, 0);
  lv_obj_set_width(*label, rect->width * 78u / 96u);
  lv_obj_set_height(*label, rect->height * 20u / 40u);
  lv_label_set_long_mode(*label, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_align(*label, LV_ALIGN_TOP_MID, 0, rect->height * 8u / 40u);
  lv_obj_remove_flag(*label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(bubble, LV_OBJ_FLAG_HIDDEN);
  return bubble;
}

lv_obj_t *lc_room_greeting_create(lv_obj_t *screen,
  const lc_ui_room_layout_t *room, const lv_image_dsc_t *source,
  const lv_font_t *font, lv_obj_t **label)
{
  lv_obj_t *bubble = lc_room_bubble_create(screen, &room->dog_bubble,
    source, room->scale, font, label, NULL, NULL);
  lv_label_set_text(*label, "你回来啦！");
  return bubble;
}
