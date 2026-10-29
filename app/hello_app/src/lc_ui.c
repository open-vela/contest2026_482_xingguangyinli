#include "lc_ui.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define LC_UI_MIN_DIMENSION 64u

static unsigned int min_uint(unsigned int first, unsigned int second)
{
  return first < second ? first : second;
}

static const char *state_label(lc_state_t state)
{
  switch (state)
    {
      case LC_STATE_IDLE:
        return "IDLE";
      case LC_STATE_GREETING:
        return "GREETING";
      case LC_STATE_LISTENING:
        return "LISTENING";
      case LC_STATE_THINKING:
        return "THINKING";
      case LC_STATE_RECOMMENDATION:
        return "RECOMMENDATION";
      case LC_STATE_CONFIRMED:
        return "CONFIRMED";
      case LC_STATE_ERROR:
        return "ERROR";
      default:
        return NULL;
    }
}

lc_ui_status_t lc_ui_build_model(lc_state_t state,
                                 bool time_synced,
                                 unsigned int hour,
                                 unsigned int minute,
                                 lc_ui_model_t *model)
{
  const char *label;
  lc_time_status_t time_status;

  if (model == NULL || (label = state_label(state)) == NULL)
    {
      return LC_UI_INVALID;
    }

  memset(model, 0, sizeof(*model));
  snprintf(model->state_label, sizeof(model->state_label), "%s", label);
  model->cat_eyes_open = state != LC_STATE_IDLE;
  model->qr_visible = state == LC_STATE_CONFIRMED;

  time_status = lc_clock_compute(time_synced, hour, minute,
                                 &model->clock_angles);
  if (time_status == LC_TIME_UNSYNCED)
    {
      snprintf(model->time_label, sizeof(model->time_label), "--:--");
      model->clock_visible = false;
      return LC_UI_TIME_UNSYNCED;
    }

  if (time_status != LC_TIME_OK)
    {
      return LC_UI_INVALID;
    }

  snprintf(model->time_label, sizeof(model->time_label), "%02u:%02u",
           hour, minute);
  model->clock_visible = true;
  return LC_UI_OK;
}

bool lc_ui_compute_layout(unsigned int width,
                          unsigned int height,
                          lc_ui_layout_t *layout)
{
  unsigned int short_side;
  unsigned int margin;
  unsigned int clock_size;
  unsigned int dialog_height;
  unsigned int qr_size;

  if (layout == NULL || width < LC_UI_MIN_DIMENSION ||
      height < LC_UI_MIN_DIMENSION)
    {
      return false;
    }

  memset(layout, 0, sizeof(*layout));
  short_side = min_uint(width, height);
  margin = short_side / 30u;
  if (margin < 2u)
    {
      margin = 2u;
    }

  clock_size = min_uint(width / 5u, height / 5u);
  dialog_height = height / 4u;

  layout->clock.x = margin;
  layout->clock.y = margin;
  layout->clock.width = clock_size;
  layout->clock.height = clock_size;

  layout->weather.x = margin + clock_size + margin;
  layout->weather.y = margin;
  layout->weather.width = width - layout->weather.x - margin;
  layout->weather.height = clock_size;

  layout->dialog.x = margin;
  layout->dialog.y = height - margin - dialog_height;
  layout->dialog.width = width - 2u * margin;
  layout->dialog.height = dialog_height;

  layout->cat.x = margin;
  layout->cat.y = margin + clock_size + margin;
  layout->cat.width = width - 2u * margin;
  layout->cat.height = layout->dialog.y - margin - layout->cat.y;

  qr_size = min_uint(dialog_height - 2u * margin, width / 5u);
  layout->qr.x = layout->dialog.x + layout->dialog.width - margin - qr_size;
  layout->qr.y = layout->dialog.y + margin;
  layout->qr.width = qr_size;
  layout->qr.height = qr_size;
  return true;
}

bool lc_ui_compute_choice_layout(unsigned int width,
                                 unsigned int height,
                                 lc_ui_choice_layout_t *layout)
{
  unsigned int short_side;
  unsigned int width_scale;
  unsigned int height_scale;
  unsigned int gap;

  if (layout == NULL || width < LC_UI_MIN_DIMENSION ||
      height < LC_UI_MIN_DIMENSION)
    {
      return false;
    }

  memset(layout, 0, sizeof(*layout));
  if (height > width)
    {
      layout->background_width = 240u;
      layout->background_height = 320u;
    }
  else
    {
      layout->background_width = 320u;
      layout->background_height = 240u;
    }

  width_scale = width * 256u / layout->background_width;
  height_scale = height * 256u / layout->background_height;
  layout->background_scale = min_uint(width_scale, height_scale);
  layout->rotate_touch = width == 240u && height == 320u;

  short_side = min_uint(width, height);
  if (width <= 320u && height <= 320u)
    {
      layout->card_size = 70u;
      layout->card_spacing = 76u;
      layout->card_bottom = 5u;
      layout->status_top = 5u;
      return true;
    }

  layout->card_size = short_side / 5u;
  if (layout->card_size < 96u)
    {
      layout->card_size = 96u;
    }
  else if (layout->card_size > 132u)
    {
      layout->card_size = 132u;
    }

  gap = short_side / 50u;
  if (gap < 10u)
    {
      gap = 10u;
    }

  layout->card_spacing = layout->card_size + gap;
  layout->card_bottom = short_side / 40u;
  if (layout->card_bottom < 12u)
    {
      layout->card_bottom = 12u;
    }

  layout->status_top = layout->card_bottom;
  return true;
}

static lc_ui_rect_t room_rect(const lc_ui_room_layout_t *room,
                              unsigned int x, unsigned int y,
                              unsigned int width, unsigned int height)
{
  lc_ui_rect_t rect;
  rect.x = room->artwork.x + x * room->scale / 256u;
  rect.y = room->artwork.y + y * room->scale / 256u;
  rect.width = width * room->scale / 256u;
  rect.height = height * room->scale / 256u;
  return rect;
}

bool lc_ui_compute_room_layout(unsigned int width,
                               unsigned int height,
                               lc_ui_room_layout_t *layout)
{
  uint64_t scale;
  if (layout == NULL || width < LC_UI_MIN_DIMENSION ||
      height < LC_UI_MIN_DIMENSION || width > 8192u || height > 8192u)
    {
      return false;
    }
  memset(layout, 0, sizeof(*layout));
  scale = (uint64_t)width * 256u / 240u;
  if (scale > (uint64_t)height * 256u / 320u)
    {
      scale = (uint64_t)height * 256u / 320u;
    }
  layout->scale = (unsigned int)scale;
  layout->artwork.width = 240u * layout->scale / 256u;
  layout->artwork.height = 320u * layout->scale / 256u;
  layout->artwork.x = (width - layout->artwork.width) / 2u;
  layout->artwork.y = (height - layout->artwork.height) / 2u;
  layout->objects[0] = room_rect(layout, 54u, 186u, 44u, 58u);
  layout->objects[1] = room_rect(layout, 101u, 204u, 44u, 40u);
  layout->objects[2] = room_rect(layout, 147u, 210u, 44u, 34u);
  layout->cat_bubble = room_rect(layout, 15u, 77u, 96u, 40u);
  layout->dog_bubble = room_rect(layout, 133u, 73u, 96u, 40u);
  layout->result = room_rect(layout, 12u, 252u, 216u, 64u);
  return true;
}
