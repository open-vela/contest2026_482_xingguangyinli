#include "lc_display.h"
#include "lc_accessory_client.h"
#include "lc_accessory_runtime.h"
#include "lc_accessory_sync.h"
#include "lc_agent.h"
#include "lc_checkin.h"
#include "lc_checkin_feedback.h"
#include "lc_native_voice.h"
#include "lc_pcm_player.h"
#include "lc_choice.h"
#include "lc_competition.h"
#include "lc_esphome_presence.h"
#include "lc_greeting.h"
#include "lc_local_accessory_server.h"
#include "lc_product_dialogue.h"
#include "lc_product_flow.h"
#include "lc_product_intent.h"
#include "lc_product_transport.h"
#include "lc_touch.h"
#include "lc_ui.h"
#include "generated/lc_choice_assets.h"

#ifdef __NuttX__

#  include <nuttx/config.h>

#  include <stdbool.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <pthread.h>
#  include <stdint.h>
#  include <stdio.h>
#  include <string.h>
#  include <sys/boardctl.h>
#  include <time.h>
#  include <unistd.h>

#  include <nuttx/input/touchscreen.h>

#  include <lvgl/lvgl.h>
#  include "lc_room_view.h"

LV_FONT_DECLARE(lc_font_cjk_14);

#  undef NEED_BOARDINIT

#  if defined(CONFIG_BOARDCTL) && !defined(CONFIG_NSH_ARCHINIT)
#    define NEED_BOARDINIT 1
#  endif

static void configure_label(lv_obj_t *label, uint32_t color)
{
  lv_obj_set_width(label, LV_PCT(88));
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(label, &lc_font_cjk_14, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
}

static void create_preview(void)
{
  lv_obj_t *screen = lv_screen_active();
  lv_obj_t *panel;
  lv_obj_t *label;

  lv_obj_set_style_bg_color(screen, lv_color_hex(0x160f2f), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  label = lv_label_create(screen);
  configure_label(label, 0xffffff);
  lv_label_set_text(label, "画间 Living Canvas");
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 12);

  label = lv_label_create(screen);
  configure_label(label, 0xc8b9ff);
  lv_label_set_text(label, "离线界面预览");
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 36);

  panel = lv_obj_create(screen);
  lv_obj_remove_style_all(panel);
  lv_obj_set_size(panel, LV_PCT(90), LV_PCT(54));
  lv_obj_align(panel, LV_ALIGN_CENTER, 0, 14);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0x2b2150), 0);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(0x8c73ff), 0);
  lv_obj_set_style_border_width(panel, 2, 0);
  lv_obj_set_style_radius(panel, 16, 0);

  label = lv_label_create(panel);
  configure_label(label, 0xffc6df);
  lv_label_set_text(label, "( =^.^= )");
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 16);

  label = lv_label_create(panel);
  configure_label(label, 0xffffff);
  lv_label_set_text(label, "一起决定今天吃什么");
  lv_obj_align(label, LV_ALIGN_CENTER, 0, 5);

  label = lv_label_create(panel);
  configure_label(label, 0xc8b9ff);
  lv_label_set_text(label, "本地规则已就绪\n智能服务连接待验证");
  lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -14);

  label = lv_label_create(screen);
  configure_label(label, 0x8ee6c4);
  lv_label_set_text(label, "显示正常 | 尚未执行设备操作");
  lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -12);
}

static lv_obj_t *g_choice_cards[LC_CHOICE_COUNT];
static const lc_choice_id_t g_choice_ids[LC_CHOICE_COUNT] =
{
  LC_CHOICE_TAKEOUT,
  LC_CHOICE_MYSTERY,
  LC_CHOICE_HOME
};
static lc_choice_state_t g_choice;
static lv_obj_t *g_choice_status;
static bool g_choice_interactive;
static bool g_product_mode;
static bool g_product_waiting;
static lv_timer_t *g_product_timer;
static lv_timer_t *g_product_voice_timer;
static lc_native_voice_t *g_product_audio;
static bool g_product_audio_ready;
static uint64_t g_product_capture_ticket;
static char g_product_voice_session[LC_PRODUCT_SESSION_MAX];
static char g_product_speech_session[LC_PRODUCT_SESSION_MAX];

typedef enum
{
  PRODUCT_VOICE_IDLE = 0,
  PRODUCT_VOICE_RECORDING,
  PRODUCT_VOICE_TRANSCRIBING,
  PRODUCT_VOICE_TRANSCRIPT_READY,
  PRODUCT_VOICE_TRANSCRIPT_ERROR,
  PRODUCT_VOICE_SPEAKING,
  PRODUCT_VOICE_SPEAK_DONE,
  PRODUCT_VOICE_SPEAK_ERROR
} product_voice_phase_t;

#define PRODUCT_VOICE_MAX_MS 15000u
#define PRODUCT_VOICE_WORKER_STACK 24576u
#define PRODUCT_PRESENCE_WORKER_STACK 12288u
#define PRODUCT_PRESENCE_PATH_MAX 96u
#define PRODUCT_CHECKIN_WORKER_STACK 12288u
#define PRODUCT_CHECKIN_PATH_MAX 96u
#define PRODUCT_LOCAL_ACCESSORY_WORKER_STACK 12288u
#define PRODUCT_LOCAL_ACCESSORY_PORT 18765u
#define PRODUCT_LOCAL_ACCESSORY_TIMEOUT_MS 200u
#define PRODUCT_LOCAL_FEEDBACK_MS 2000u
#define PRODUCT_LOCAL_CHIME_SAMPLES 2880u
#define PRODUCT_REMOTE_WORKER_STACK 24576u
#define PRODUCT_REMOTE_POLL_US 400000u
#define PRODUCT_REMOTE_BACKOFF_US 2000000u

static pthread_mutex_t g_product_voice_lock = PTHREAD_MUTEX_INITIALIZER;
static product_voice_phase_t g_product_voice_phase = PRODUCT_VOICE_IDLE;
static uint32_t g_product_voice_started_ms;
static char g_product_transcript[LC_PRODUCT_TRANSCRIPT_BYTES];
static char g_product_speech[LC_AGENT_REPLY_MAX];
static lc_product_flow_t g_product_flow;
static lc_product_transport_t g_product_transport;
static bool g_product_transport_ready;
static lv_obj_t *g_product_panel;
static lv_obj_t *g_product_title;
static lv_obj_t *g_product_detail;
static lv_obj_t *g_product_badge;
static lv_obj_t *g_product_cat_bubble;
static lv_obj_t *g_product_dog_bubble;
static lc_ui_room_layout_t g_product_room;
static bool g_product_dialogue_started;
static lc_product_state_t g_product_dialogue_state;
static uint32_t g_product_dialogue_started_ms;
#  if LV_USE_QRCODE
static lv_obj_t *g_product_qr;
#  endif

typedef enum
{
  PRODUCT_BACKEND_IDLE = 0,
  PRODUCT_BACKEND_RECOMMENDING,
  PRODUCT_BACKEND_RECOMMEND_READY,
  PRODUCT_BACKEND_CONFIRMING,
  PRODUCT_BACKEND_HANDOFF_READY,
  PRODUCT_BACKEND_ERROR
} product_backend_phase_t;

static pthread_mutex_t g_product_backend_lock = PTHREAD_MUTEX_INITIALIZER;
static product_backend_phase_t g_product_backend_phase = PRODUCT_BACKEND_IDLE;
static lc_product_transport_status_t g_product_backend_status;
static lc_choice_id_t g_product_backend_choice;
static bool g_product_backend_reject;
static lc_product_result_t g_product_backend_result;
static char g_product_backend_transcript[LC_PRODUCT_TRANSCRIPT_BYTES];
static char g_product_session[LC_PRODUCT_SESSION_MAX];
static char g_product_handoff[LC_PRODUCT_URL_MAX];
static lc_competition_t g_competition;
static lv_obj_t *g_competition_panel;
static lv_obj_t *g_competition_title;
static lv_obj_t *g_competition_detail;
static lv_obj_t *g_competition_badge;
#  if LV_USE_QRCODE
static lv_obj_t *g_competition_qr;
#  endif
static lv_timer_t *g_competition_timer;
static unsigned int g_competition_step;
static char g_competition_handoff[LC_COMPETITION_QR_URL_MAX];
static char g_competition_detail_text[LC_COMPETITION_REASON_MAX +
                                      LC_COMPETITION_PRICE_MAX + 4u];
static bool g_project_demo;
static lc_greeting_animation_t g_greeting_animation;
static lv_obj_t *g_greeting_panel;
static lv_obj_t *g_greeting_message;
static lv_timer_t *g_greeting_timer;
static pthread_mutex_t g_product_presence_lock = PTHREAD_MUTEX_INITIALIZER;
static lc_accessory_runtime_t g_product_presence_runtime;
static lc_state_machine_t g_product_presence_state;
static bool g_product_presence_ready;
static bool g_product_presence_pending;
static bool g_product_presence_error;
static bool g_product_presence_error_shown;
static char g_product_presence_path[PRODUCT_PRESENCE_PATH_MAX];
static lv_timer_t *g_product_presence_timer;
static pthread_mutex_t g_product_checkin_lock = PTHREAD_MUTEX_INITIALIZER;
static lc_checkin_runtime_t g_product_checkin_runtime;
static bool g_product_checkin_runtime_ready;
static bool g_product_checkin_ready;
static bool g_product_checkin_pending;
static bool g_product_checkin_error;
static bool g_product_checkin_error_shown;
static char g_product_checkin_path[PRODUCT_CHECKIN_PATH_MAX];
static char g_product_checkin_status[64];
static lv_timer_t *g_product_checkin_timer;
static lc_local_accessory_server_t g_product_local_accessory_server;
static bool g_product_local_accessory_ready;
static bool g_product_local_accessory_error;
static bool g_product_local_accessory_error_shown;
static lc_checkin_feedback_t g_product_checkin_feedback;
static lc_checkin_feedback_identity_t g_product_local_chime_identity;
static bool g_product_local_chime_running;
static pthread_mutex_t g_product_remote_lock = PTHREAD_MUTEX_INITIALIZER;
static lc_accessory_client_t g_product_remote_client;
static lc_accessory_sync_t g_product_remote_sync;
static bool g_product_remote_ready;
static bool g_product_remote_greeting_pending;
static bool g_product_remote_checkin_pending;
static bool g_product_remote_finish_pending;
static char g_product_remote_checkin_status[LC_ACCESSORY_SYNC_STATUS_MAX];
static lv_timer_t *g_product_remote_timer;

static void choice_card_event_cb(lv_event_t *event);
static void product_voice_event_cb(lv_event_t *event);
static void product_result_event_cb(lv_event_t *event);
static void poll_product_backend(lv_timer_t *timer);
static bool product_voice_busy(void);

static uint32_t monotonic_ms(void)
{
  struct timespec current;
  uint64_t milliseconds;

  if (clock_gettime(CLOCK_MONOTONIC, &current) != 0)
    {
      return 0u;
    }

  milliseconds = (uint64_t)current.tv_sec * 1000u;
  milliseconds += (uint64_t)current.tv_nsec / 1000000u;
  return (uint32_t)milliseconds;
}

static void finish_product_presence_state(void)
{
  if (g_product_remote_ready)
    {
      pthread_mutex_lock(&g_product_remote_lock);
      g_product_remote_finish_pending = true;
      pthread_mutex_unlock(&g_product_remote_lock);
      return;
    }

  if (g_product_presence_ready)
    {
      pthread_mutex_lock(&g_product_presence_lock);
      (void)lc_accessory_runtime_finish_greeting(
        &g_product_presence_runtime, monotonic_ms());
      pthread_mutex_unlock(&g_product_presence_lock);
    }
}

static void finish_greeting_overlay(void)
{
  g_greeting_animation.active = false;
  lv_obj_add_flag(g_greeting_panel, LV_OBJ_FLAG_HIDDEN);
  lv_timer_pause(g_greeting_timer);
  finish_product_presence_state();
}

static void poll_greeting_animation(lv_timer_t *timer)
{
  lc_greeting_frame_t frame;
  lc_greeting_status_t status;

  (void)timer;

  status = lc_greeting_animation_sample(&g_greeting_animation,
                                        monotonic_ms(), &frame);
  if (status == LC_GREETING_DONE)
    {
      finish_greeting_overlay();
      return;
    }

  if (status != LC_GREETING_PLAYING)
    {
      return;
    }

  /* A short head-level greeting, not a video or an opaque character panel. */
}

static void start_greeting_overlay(void)
{
  if (g_greeting_panel == NULL || g_greeting_timer == NULL)
    {
      return;
    }

  /* Presence must never obscure an active conversation or QR handoff. */
  if (g_product_mode && (g_product_waiting || product_voice_busy() ||
                        g_product_flow.state != LC_PRODUCT_CHOICE))
    {
      finish_product_presence_state();
      return;
    }

  lc_greeting_animation_start(&g_greeting_animation, monotonic_ms());
  lv_obj_remove_flag(g_greeting_panel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(g_greeting_panel);
  lv_timer_resume(g_greeting_timer);
}

static bool create_greeting_overlay(lv_obj_t *screen, bool autoplay)
{
  lc_ui_room_layout_t room;
  lv_display_t *display = lv_obj_get_display(screen);
  static const lv_image_dsc_t bubble = {
    .header = {.magic = LV_IMAGE_HEADER_MAGIC,
               .cf = LV_COLOR_FORMAT_ARGB8888,
               .w = LC_BUBBLE_WIDTH, .h = LC_BUBBLE_HEIGHT,
               .stride = LC_BUBBLE_WIDTH * 4u},
    .data_size = LC_BUBBLE_BYTES, .data = lc_bubble_dog
  };
  if (!lc_ui_compute_room_layout(
        (unsigned int)lv_display_get_horizontal_resolution(display),
        (unsigned int)lv_display_get_vertical_resolution(display), &room))
    {
      return false;
    }
  g_greeting_panel = lc_room_greeting_create(screen, &room, &bubble,
    &lc_font_cjk_14, &g_greeting_message);

  lc_greeting_animation_init(&g_greeting_animation, 1800u);
  g_greeting_timer = lv_timer_create(poll_greeting_animation, 80u, NULL);
  if (g_greeting_timer == NULL)
    {
      return false;
    }

  lv_obj_add_flag(g_greeting_panel, LV_OBJ_FLAG_HIDDEN);
  lv_timer_pause(g_greeting_timer);
  if (autoplay)
    {
      start_greeting_overlay();
    }

  return true;
}

static int g_touch_fd = -1;
static lv_indev_state_t g_touch_state = LV_INDEV_STATE_RELEASED;
static lc_touch_point_t g_touch_point;

static void portrait_touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
  struct touch_sample_s sample;
  lv_display_t *display = lv_indev_get_display(indev);
  ssize_t nbytes;

  nbytes = read(g_touch_fd, &sample, sizeof(sample));
  if (nbytes == (ssize_t)sizeof(sample) && sample.npoints > 0)
    {
      uint8_t flags = sample.point[0].flags;

      if ((flags & (TOUCH_DOWN | TOUCH_MOVE)) != 0)
        {
          g_touch_point = lc_touch_transform(
            sample.point[0].x, sample.point[0].y,
            lv_display_get_horizontal_resolution(display),
            lv_display_get_vertical_resolution(display), true);
          g_touch_state = LV_INDEV_STATE_PRESSED;
        }
      else if ((flags & TOUCH_UP) != 0)
        {
          g_touch_state = LV_INDEV_STATE_RELEASED;
        }
    }

  data->point.x = g_touch_point.x;
  data->point.y = g_touch_point.y;
  data->state = g_touch_state;
}

static void configure_portrait_touch(lv_nuttx_result_t *result)
{
  void *driver_data;

  if (result->indev == NULL || result->disp == NULL ||
      lv_display_get_horizontal_resolution(result->disp) != 240 ||
      lv_display_get_vertical_resolution(result->disp) != 320)
    {
      return;
    }

  driver_data = lv_indev_get_driver_data(result->indev);
  if (driver_data == NULL)
    {
      return;
    }

  g_touch_fd = *(int *)driver_data;
  g_touch_state = LV_INDEV_STATE_RELEASED;
  g_touch_point.x = 0;
  g_touch_point.y = 0;
  lv_indev_set_read_cb(result->indev, portrait_touch_read);
}

static void init_image_descriptor(lv_image_dsc_t *descriptor,
                                  const uint8_t *data,
                                  uint32_t data_size,
                                  uint32_t width,
                                  uint32_t height)
{
  memset(descriptor, 0, sizeof(*descriptor));
  descriptor->header.magic = LV_IMAGE_HEADER_MAGIC;
  descriptor->header.cf = LV_COLOR_FORMAT_RGB565;
  descriptor->header.w = width;
  descriptor->header.h = height;
  descriptor->header.stride = width * 2u;
  descriptor->data_size = data_size;
  descriptor->data = data;
}

static bool create_background(lv_display_t *display, lv_obj_t *screen)
{
  static lv_image_dsc_t background;
  int32_t width = lv_display_get_horizontal_resolution(display);
  int32_t height = lv_display_get_vertical_resolution(display);
  lc_ui_choice_layout_t layout;
  lv_obj_t *image;

  if (!lc_ui_compute_choice_layout((unsigned int)width,
                                   (unsigned int)height, &layout))
    {
      fprintf(stderr, "Living Canvas unsupported display: %ldx%ld\n",
              (long)width, (long)height);
      return false;
    }

  if (g_product_mode)
    {
      if (!lc_ui_compute_room_layout((unsigned int)width,
                                      (unsigned int)height, &g_product_room))
        {
          return false;
        }
      layout.background_width = LC_BG_PORTRAIT_WIDTH;
      layout.background_height = LC_BG_PORTRAIT_HEIGHT;
      layout.background_scale = g_product_room.scale;
    }

  if (layout.background_width == LC_BG_PORTRAIT_WIDTH &&
      layout.background_height == LC_BG_PORTRAIT_HEIGHT)
    {
      init_image_descriptor(&background, lc_bg_portrait,
                            LC_BG_PORTRAIT_BYTES,
                            LC_BG_PORTRAIT_WIDTH,
                            LC_BG_PORTRAIT_HEIGHT);
    }
  else
    {
      init_image_descriptor(&background, lc_bg_landscape,
                            LC_BG_LANDSCAPE_BYTES,
                            LC_BG_LANDSCAPE_WIDTH,
                            LC_BG_LANDSCAPE_HEIGHT);
    }
  lv_obj_remove_style_all(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x1b1024), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  image = lv_image_create(screen);
  lv_image_set_src(image, &background);
  lv_image_set_scale(image, layout.background_scale);
  lv_obj_center(image);
  return true;
}

static void style_choice_card(lv_obj_t *card, bool selected, bool confirmed)
{
  if (g_product_mode)
    {
      lv_obj_set_style_border_width(card, selected ? 2 : 0, 0);
      lv_obj_set_style_border_color(card,
        lv_color_hex(confirmed ? 0x65d48a : 0xffd66b), 0);
      lv_obj_set_style_border_opa(card,
                                 selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
      return;
    }
  lv_obj_set_style_border_width(card, selected ? 4 : 2, 0);
  lv_obj_set_style_border_color(
    card, lv_color_hex(confirmed ? 0x65d48a
                                : (selected ? 0xffd66b : 0x7d604e)), 0);
  lv_obj_set_style_border_opa(card,
                              selected ? LV_OPA_COVER : LV_OPA_70, 0);
}

static lv_obj_t *create_room_hotspot(lv_obj_t *screen,
                                    lc_choice_id_t choice,
                                    bool interactive)
{
  const lc_ui_rect_t *rect = &g_product_room.objects[choice];
  lv_obj_t *hotspot = lc_room_hotspot_create(screen, rect,
    interactive ? choice_card_event_cb : NULL, (void *)&g_choice_ids[choice]);
  style_choice_card(hotspot, false, false);
  return hotspot;
}

static lv_obj_t *create_choice_card(lv_obj_t *screen,
                                    const lv_image_dsc_t *source,
                                    int32_t x_offset,
                                    const lc_ui_choice_layout_t *layout,
                                    lc_choice_id_t choice,
                                    bool interactive)
{
  lv_obj_t *card = lv_obj_create(screen);
  lv_obj_t *image;

  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, layout->card_size, layout->card_size);
  lv_obj_align(card, LV_ALIGN_BOTTOM_MID, x_offset,
               -(int32_t)layout->card_bottom);
  lv_obj_set_style_bg_color(card, lv_color_hex(0xf6e1c2), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 10, 0);
  style_choice_card(card, choice == g_choice.selected, false);

  if (interactive)
    {
      lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(card, choice_card_event_cb, LV_EVENT_CLICKED,
                          (void *)&g_choice_ids[choice]);
    }

  image = lv_image_create(card);
  lv_image_set_src(image, source);
  if (layout->card_size > 70u)
    {
      lv_image_set_scale(image,
                         (layout->card_size - 16u) * 256u / LC_CARD_WIDTH);
    }
  lv_obj_center(image);
  lv_obj_remove_flag(image, LV_OBJ_FLAG_CLICKABLE);
  return card;
}

static const char *choice_name(lc_choice_id_t choice)
{
  switch (choice)
    {
      case LC_CHOICE_TAKEOUT:
        return "点外卖";
      case LC_CHOICE_MYSTERY:
        return "口味盲盒";
      case LC_CHOICE_HOME:
        return "在家做饭";
      case LC_CHOICE_COUNT:
        break;
    }

  return "未选择";
}

static void render_choice_state(void)
{
  static char status[48];
  unsigned int index;

  for (index = 0u; index < LC_CHOICE_COUNT; index++)
    {
      bool selected = g_choice.selected == (lc_choice_id_t)index &&
                      (!g_product_mode || g_choice.armed || g_choice.confirmed);
      bool confirmed = selected && g_choice.confirmed;

      style_choice_card(g_choice_cards[index], selected, confirmed);
    }

  if (g_choice_status == NULL)
    {
      return;
    }

  if (g_choice.confirmed)
    {
      (void)snprintf(status, sizeof(status), "已选择：%s",
                     choice_name(g_choice.selected));
      lv_label_set_text(g_choice_status, status);
      lv_obj_set_style_text_color(g_choice_status,
                                  lv_color_hex(0x65d48a), 0);
    }
  else if (g_choice.armed)
    {
      lv_label_set_text(g_choice_status, "再点一次选择");
      lv_obj_set_style_text_color(g_choice_status,
                                  lv_color_hex(0xffd66b), 0);
    }
  else
    {
      lv_label_set_text(g_choice_status, "选择桌上的物品");
      lv_obj_set_style_text_color(g_choice_status,
                                  lv_color_hex(0xffffff), 0);
    }
}

static void set_choice_status(const char *text, uint32_t color)
{
  if (g_choice_status != NULL)
    {
      lv_label_set_text(g_choice_status, text);
      lv_obj_set_style_text_color(g_choice_status, lv_color_hex(color), 0);
    }
}

static bool spawn_product_voice_worker(void *(*entry)(void *))
{
  pthread_attr_t attributes;
  pthread_t thread;
  int result;

  if (pthread_attr_init(&attributes) != 0)
    {
      return false;
    }

  (void)pthread_attr_setstacksize(&attributes, PRODUCT_VOICE_WORKER_STACK);
  (void)pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
  result = pthread_create(&thread, &attributes, entry, NULL);
  pthread_attr_destroy(&attributes);
  return result == 0;
}

static void set_product_presence_error(void)
{
  pthread_mutex_lock(&g_product_presence_lock);
  g_product_presence_error = true;
  pthread_mutex_unlock(&g_product_presence_lock);
}

static void product_mmwave_observed(void *context, bool detected)
{
  lc_accessory_runtime_result_t result;
  lc_accessory_runtime_status_t status;

  (void)context;
  pthread_mutex_lock(&g_product_presence_lock);
  status = lc_accessory_runtime_accept_binary(
    &g_product_presence_runtime, detected, monotonic_ms(), &result);
  if (status == LC_ACCESSORY_RUNTIME_GREETING)
    {
      g_product_presence_pending = true;
    }
  pthread_mutex_unlock(&g_product_presence_lock);
}

static void *product_presence_worker(void *unused)
{
  lc_accessory_runtime_result_t result;
  lc_esphome_presence_t esphome;
  uint8_t buffer[64];
  bool direct_mmwave;
  bool writable;
  int fd;

  (void)unused;
  direct_mmwave =
    g_product_presence_runtime.source == LC_PRESENCE_SOURCE_MMWAVE;
  writable = !direct_mmwave;
  if (direct_mmwave)
    {
      fd = open(g_product_presence_path, O_RDONLY | O_NONBLOCK);
      lc_esphome_presence_init(&esphome);
    }
  else
    {
      fd = open(g_product_presence_path, O_RDWR | O_NONBLOCK);
      if (fd < 0)
        {
          writable = false;
          fd = open(g_product_presence_path, O_RDONLY | O_NONBLOCK);
        }
    }

  if (fd < 0)
    {
      set_product_presence_error();
      return NULL;
    }

  for (;;)
    {
      ssize_t count = read(fd, buffer, sizeof(buffer));
      ssize_t index;

      if (count > 0)
        {
          if (direct_mmwave)
            {
              (void)lc_esphome_presence_feed(
                &esphome, buffer, (size_t)count, product_mmwave_observed,
                NULL);
              continue;
            }

          for (index = 0; index < count; index++)
            {
              lc_accessory_runtime_status_t status;

              pthread_mutex_lock(&g_product_presence_lock);
              status = lc_accessory_runtime_feed(
                &g_product_presence_runtime, buffer[index], monotonic_ms(),
                &result);
              if (status == LC_ACCESSORY_RUNTIME_GREETING)
                {
                  g_product_presence_pending = true;
                }
              pthread_mutex_unlock(&g_product_presence_lock);

              if (!direct_mmwave && writable &&
                  status != LC_ACCESSORY_RUNTIME_NONE &&
                  result.ack[0] != '\0')
                {
                  (void)write(fd, result.ack, strlen(result.ack));
                  (void)write(fd, "\n", 1u);
                }
            }
          continue;
        }

      if (count == 0 || errno == EAGAIN || errno == EWOULDBLOCK)
        {
          usleep(20000u);
          continue;
        }

      if (errno == EINTR)
        {
          continue;
        }

      set_product_presence_error();
      close(fd);
      return NULL;
    }
}

static bool start_product_presence_reader(const char *event_path,
                                          lc_presence_source_t source)
{
  pthread_attr_t attributes;
  pthread_t thread;
  size_t length;
  int result;

  if (event_path == NULL || event_path[0] != '/')
    {
      return false;
    }

  for (length = 0u;
       length < sizeof(g_product_presence_path) && event_path[length] != '\0';
       length++)
    {
    }
  if (length == 0u || length >= sizeof(g_product_presence_path))
    {
      return false;
    }

  memcpy(g_product_presence_path, event_path, length + 1u);
  lc_state_init(&g_product_presence_state, 30000u);
  if (!lc_accessory_runtime_init(&g_product_presence_runtime,
                                 &g_product_presence_state, source,
                                 70u, 1200u))
    {
      return false;
    }

  g_product_presence_ready = true;
  if (pthread_attr_init(&attributes) != 0)
    {
      return false;
    }

  (void)pthread_attr_setstacksize(&attributes, PRODUCT_PRESENCE_WORKER_STACK);
  (void)pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
  result = pthread_create(&thread, &attributes,
                          product_presence_worker, NULL);
  pthread_attr_destroy(&attributes);
  if (result != 0)
    {
      g_product_presence_ready = false;
      return false;
    }

  return true;
}

static void poll_product_presence(lv_timer_t *timer)
{
  bool pending;
  bool error;

  (void)timer;
  pthread_mutex_lock(&g_product_presence_lock);
  pending = g_product_presence_pending;
  error = g_product_presence_error;
  g_product_presence_pending = false;
  pthread_mutex_unlock(&g_product_presence_lock);

  if (pending)
    {
      start_greeting_overlay();
    }

  if (error && !g_product_presence_error_shown)
    {
      g_product_presence_error_shown = true;
      set_choice_status("感应设备未连接", 0xffd66b);
    }
}

static void set_product_checkin_error(void)
{
  pthread_mutex_lock(&g_product_checkin_lock);
  g_product_checkin_error = true;
  pthread_mutex_unlock(&g_product_checkin_lock);
}

static void *product_checkin_worker(void *unused)
{
  lc_checkin_result_t result;
  uint8_t buffer[64];
  bool writable = true;
  int fd;

  (void)unused;
  fd = open(g_product_checkin_path, O_RDWR | O_NONBLOCK);
  if (fd < 0)
    {
      writable = false;
      fd = open(g_product_checkin_path, O_RDONLY | O_NONBLOCK);
    }

  if (fd < 0)
    {
      set_product_checkin_error();
      return NULL;
    }

  for (;;)
    {
      ssize_t count = read(fd, buffer, sizeof(buffer));
      ssize_t index;

      if (count > 0)
        {
          for (index = 0; index < count; index++)
            {
              lc_checkin_status_t status;

              pthread_mutex_lock(&g_product_checkin_lock);
              status = lc_checkin_runtime_feed(&g_product_checkin_runtime,
                                                buffer[index], &result);
              if (status == LC_CHECKIN_ACCEPTED &&
                  lc_checkin_format_status(&g_product_checkin_runtime.tracker,
                                           g_product_checkin_status,
                                           sizeof(g_product_checkin_status)))
                {
                  g_product_checkin_pending = true;
                }
              pthread_mutex_unlock(&g_product_checkin_lock);

              if (writable && status != LC_CHECKIN_IGNORED &&
                  result.ack[0] != '\0')
                {
                  (void)write(fd, result.ack, strlen(result.ack));
                  (void)write(fd, "\n", 1u);
                }
            }
          continue;
        }

      if (count == 0 || errno == EAGAIN || errno == EWOULDBLOCK)
        {
          usleep(20000u);
          continue;
        }

      if (errno == EINTR)
        {
          continue;
        }

      set_product_checkin_error();
      close(fd);
      return NULL;
    }
}

static bool start_product_checkin_reader(const char *checkin_event_path)
{
  pthread_attr_t attributes;
  pthread_t thread;
  size_t length;
  int result;

  if (checkin_event_path == NULL || checkin_event_path[0] != '/')
    {
      return false;
    }

  for (length = 0u;
       length < sizeof(g_product_checkin_path) &&
       checkin_event_path[length] != '\0';
       length++)
    {
    }
  if (length == 0u || length >= sizeof(g_product_checkin_path))
    {
      return false;
    }

  memcpy(g_product_checkin_path, checkin_event_path, length + 1u);
  if (!g_product_checkin_runtime_ready &&
      (!lc_checkin_runtime_init(&g_product_checkin_runtime) ||
       !lc_checkin_format_status(&g_product_checkin_runtime.tracker,
                                 g_product_checkin_status,
                                 sizeof(g_product_checkin_status))))
    {
      return false;
    }
  g_product_checkin_runtime_ready = true;

  g_product_checkin_ready = true;
  if (pthread_attr_init(&attributes) != 0)
    {
      g_product_checkin_ready = false;
      return false;
    }

  (void)pthread_attr_setstacksize(&attributes, PRODUCT_CHECKIN_WORKER_STACK);
  (void)pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
  result = pthread_create(&thread, &attributes, product_checkin_worker, NULL);
  pthread_attr_destroy(&attributes);
  if (result != 0)
    {
      g_product_checkin_ready = false;
      return false;
    }

  return true;
}

static bool accept_product_local_accessory(
  void *unused, const lc_local_accessory_event_t *local_event)
{
  lc_accessory_event_t event;
  lc_checkin_result_t result;
  lc_checkin_status_t status;
  bool delivered = false;

  (void)unused;
  if (local_event == NULL)
    {
      return false;
    }

  memset(&event, 0, sizeof(event));
  memcpy(event.device_id, local_event->device_id,
         strlen(local_event->device_id) + 1u);
  memcpy(event.session_id, local_event->session_id,
         strlen(local_event->session_id) + 1u);
  event.sequence = local_event->sequence;
  event.type = LC_ACCESSORY_CHECKIN;
  event.value = 1;

  pthread_mutex_lock(&g_product_checkin_lock);
  if (g_product_checkin_runtime_ready)
    {
      status = lc_checkin_tracker_accept(&g_product_checkin_runtime.tracker,
                                         &event, &result);
      delivered = status == LC_CHECKIN_ACCEPTED ||
                  status == LC_CHECKIN_DUPLICATE;
      if (status == LC_CHECKIN_ACCEPTED &&
          lc_checkin_format_status(&g_product_checkin_runtime.tracker,
                                   g_product_checkin_status,
                                   sizeof(g_product_checkin_status)))
        {
          g_product_checkin_pending =
            lc_checkin_feedback_on_accepted(&g_product_checkin_feedback,
                                            local_event->device_id,
                                            monotonic_ms());
        }
    }
  pthread_mutex_unlock(&g_product_checkin_lock);
  return delivered;
}

static void *product_local_accessory_worker(void *unused)
{
  (void)unused;
  for (;;)
    {
      const lc_local_accessory_server_status_t status =
        lc_local_accessory_server_poll(&g_product_local_accessory_server);
      if (status == LC_LOCAL_ACCESSORY_SERVER_STOPPED)
        {
          return NULL;
        }
      if (status == LC_LOCAL_ACCESSORY_SERVER_IO_ERROR)
        {
          pthread_mutex_lock(&g_product_checkin_lock);
          g_product_local_accessory_error = true;
          pthread_mutex_unlock(&g_product_checkin_lock);
          return NULL;
        }
    }
}

static void mark_product_local_accessory_error(void)
{
  pthread_mutex_lock(&g_product_checkin_lock);
  g_product_local_accessory_error = true;
  pthread_mutex_unlock(&g_product_checkin_lock);
}

static bool start_product_local_accessory_server(void)
{
  lc_local_accessory_keys_t keys;
  lc_local_accessory_server_config_t config;
  pthread_attr_t attributes;
  pthread_t thread;
  int result;

  if (!g_product_checkin_runtime_ready)
    {
      mark_product_local_accessory_error();
      return false;
    }
  if (!lc_local_accessory_keys_load(LC_LOCAL_ACCESSORY_KEYS_CONFIG_PATH,
                                    &keys))
    {
      mark_product_local_accessory_error();
      return false;
    }
  memset(&config, 0, sizeof(config));
  config.port = PRODUCT_LOCAL_ACCESSORY_PORT;
  config.receive_timeout_ms = PRODUCT_LOCAL_ACCESSORY_TIMEOUT_MS;
  memcpy(config.cat_a_key, keys.cat_a, sizeof(config.cat_a_key));
  memcpy(config.cat_b_key, keys.cat_b, sizeof(config.cat_b_key));
  config.hmac = lc_local_accessory_mbedtls_hmac;
  config.on_event = accept_product_local_accessory;
  memset(&keys, 0, sizeof(keys));

  if (!lc_local_accessory_server_init(&g_product_local_accessory_server,
                                      &config))
    {
      memset(&config, 0, sizeof(config));
      mark_product_local_accessory_error();
      return false;
    }
  memset(&config, 0, sizeof(config));
  if (!lc_local_accessory_server_start(&g_product_local_accessory_server))
    {
      mark_product_local_accessory_error();
      return false;
    }

  if (pthread_attr_init(&attributes) != 0)
    {
      lc_local_accessory_server_stop(&g_product_local_accessory_server);
      mark_product_local_accessory_error();
      return false;
    }
  (void)pthread_attr_setstacksize(&attributes,
                                  PRODUCT_LOCAL_ACCESSORY_WORKER_STACK);
  (void)pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
  result = pthread_create(&thread, &attributes,
                          product_local_accessory_worker, NULL);
  pthread_attr_destroy(&attributes);
  if (result != 0)
    {
      lc_local_accessory_server_stop(&g_product_local_accessory_server);
      mark_product_local_accessory_error();
      return false;
    }
  g_product_local_accessory_ready = true;
  return true;
}

static bool product_local_chime_current(void *unused)
{
  (void)unused;
  return true;
}

static void *product_local_chime_worker(void *unused)
{
  static uint8_t pcm[PRODUCT_LOCAL_CHIME_SAMPLES * 2u];
  lc_checkin_feedback_identity_t identity;
  unsigned int period;
  unsigned int sample_index;

  (void)unused;
  pthread_mutex_lock(&g_product_checkin_lock);
  identity = g_product_local_chime_identity;
  pthread_mutex_unlock(&g_product_checkin_lock);
  period = identity == LC_CHECKIN_FEEDBACK_CAT_A ? 27u : 36u;

  for (sample_index = 0u; sample_index < PRODUCT_LOCAL_CHIME_SAMPLES;
       sample_index++)
    {
      const unsigned int phase = sample_index % period;
      const unsigned int half = period / 2u;
      unsigned int envelope = 240u;
      int32_t triangle;
      int16_t sample;

      if (sample_index < envelope)
        {
          envelope = sample_index;
        }
      if (PRODUCT_LOCAL_CHIME_SAMPLES - 1u - sample_index < envelope)
        {
          envelope = PRODUCT_LOCAL_CHIME_SAMPLES - 1u - sample_index;
        }
      if (phase < half)
        {
          triangle = -3000 + (int32_t)(phase * 6000u / half);
        }
      else
        {
          triangle = 3000 -
            (int32_t)((phase - half) * 6000u / (period - half));
        }
      sample = (int16_t)(triangle * (int32_t)envelope / 240);
      pcm[sample_index * 2u] = (uint8_t)((uint16_t)sample & 0xffu);
      pcm[sample_index * 2u + 1u] =
        (uint8_t)(((uint16_t)sample >> 8) & 0xffu);
    }

  (void)lc_pcm_player_play_native(pcm, sizeof(pcm),
                                  product_local_chime_current, NULL);
  memset(pcm, 0, sizeof(pcm));
  pthread_mutex_lock(&g_product_checkin_lock);
  g_product_local_chime_running = false;
  pthread_mutex_unlock(&g_product_checkin_lock);
  return NULL;
}

static void poll_product_checkin(lv_timer_t *timer)
{
  bool pending;
  bool error;
  bool local_error;
  bool feedback_was_active;
  bool feedback_active;
  bool voice_busy;
  char feedback_message[32];
  char status_text[sizeof(g_product_checkin_status)];
  lc_checkin_feedback_identity_t chime = LC_CHECKIN_FEEDBACK_NONE;

  (void)timer;
  voice_busy = product_voice_busy();
  pthread_mutex_lock(&g_product_checkin_lock);
  feedback_was_active =
    lc_checkin_feedback_active(&g_product_checkin_feedback);
  lc_checkin_feedback_update(&g_product_checkin_feedback, monotonic_ms());
  feedback_active =
    lc_checkin_feedback_active(&g_product_checkin_feedback);
  pending = g_product_checkin_pending;
  error = g_product_checkin_error;
  local_error = g_product_local_accessory_error;
  g_product_checkin_pending = false;
  memcpy(status_text, g_product_checkin_status, sizeof(status_text));
  status_text[sizeof(status_text) - 1u] = '\0';
  feedback_message[0] = '\0';
  if (feedback_active)
    {
      (void)snprintf(feedback_message, sizeof(feedback_message), "%s",
                     lc_checkin_feedback_message(&g_product_checkin_feedback));
    }
  if (!voice_busy && !g_product_local_chime_running)
    {
      chime = lc_checkin_feedback_take_chime(&g_product_checkin_feedback);
      if (chime != LC_CHECKIN_FEEDBACK_NONE)
        {
          g_product_local_chime_identity = chime;
          g_product_local_chime_running = true;
        }
    }
  pthread_mutex_unlock(&g_product_checkin_lock);

  if (pending && feedback_message[0] != '\0')
    {
      if (!g_product_waiting && g_product_flow.state == LC_PRODUCT_CHOICE)
        {
          set_choice_status(feedback_message, 0x65d48a);
        }
    }
  else if (pending)
    {
      set_choice_status(status_text, 0x65d48a);
    }
  if (feedback_was_active && !feedback_active && !g_product_waiting &&
      g_product_flow.state == LC_PRODUCT_CHOICE)
    {
      render_choice_state();
    }
  if (chime != LC_CHECKIN_FEEDBACK_NONE &&
      !spawn_product_voice_worker(product_local_chime_worker))
    {
      pthread_mutex_lock(&g_product_checkin_lock);
      g_product_local_chime_running = false;
      pthread_mutex_unlock(&g_product_checkin_lock);
    }

  if (error && !g_product_checkin_error_shown)
    {
      g_product_checkin_error_shown = true;
      set_choice_status("打卡设备未连接", 0xffd66b);
    }

  if (local_error && !g_product_local_accessory_error_shown)
    {
      g_product_local_accessory_error_shown = true;
      set_choice_status("本地打卡暂不可用", 0xffd66b);
    }
}

static void *product_remote_worker(void *unused)
{
  (void)unused;
  for (;;)
    {
      lc_accessory_sync_result_t result;
      lc_accessory_client_status_t status;
      bool finish;

      pthread_mutex_lock(&g_product_remote_lock);
      finish = g_product_remote_finish_pending;
      g_product_remote_finish_pending = false;
      pthread_mutex_unlock(&g_product_remote_lock);
      if (finish)
        {
          (void)lc_accessory_sync_finish_greeting(&g_product_remote_sync,
                                                   monotonic_ms());
        }

      status = lc_accessory_sync_poll(&g_product_remote_sync,
                                      monotonic_ms(), &result);
      if (status == LC_ACCESSORY_CLIENT_OK)
        {
          pthread_mutex_lock(&g_product_remote_lock);
          if (result.greeting_requested)
            {
              g_product_remote_greeting_pending = true;
            }
          if (result.checkin_changed)
            {
              memcpy(g_product_remote_checkin_status,
                     result.checkin_status,
                     sizeof(g_product_remote_checkin_status));
              g_product_remote_checkin_status[
                sizeof(g_product_remote_checkin_status) - 1u] = '\0';
              g_product_remote_checkin_pending = true;
            }
          pthread_mutex_unlock(&g_product_remote_lock);
          usleep(PRODUCT_REMOTE_POLL_US);
        }
      else
        {
          /* Accessory relay failures never stop touch or voice interaction. */
          usleep(PRODUCT_REMOTE_BACKOFF_US);
        }
    }

  return NULL;
}

static bool start_product_remote_accessories(void)
{
  pthread_attr_t attributes;
  pthread_t thread;
  int result;

  if (!g_product_transport_ready ||
      !lc_accessory_client_init_default(&g_product_remote_client,
                                        &g_product_transport) ||
      !lc_accessory_sync_init(&g_product_remote_sync,
                              &g_product_remote_client))
    {
      lc_accessory_client_clear(&g_product_remote_client);
      return false;
    }

  if (!lc_checkin_format_status(&g_product_remote_sync.checkins,
                                g_product_remote_checkin_status,
                                sizeof(g_product_remote_checkin_status)) ||
      pthread_attr_init(&attributes) != 0)
    {
      lc_accessory_client_clear(&g_product_remote_client);
      return false;
    }

  g_product_remote_ready = true;
  (void)pthread_attr_setstacksize(&attributes, PRODUCT_REMOTE_WORKER_STACK);
  (void)pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
  result = pthread_create(&thread, &attributes, product_remote_worker, NULL);
  pthread_attr_destroy(&attributes);
  if (result != 0)
    {
      g_product_remote_ready = false;
      lc_accessory_client_clear(&g_product_remote_client);
      return false;
    }

  return true;
}

static void poll_product_remote(lv_timer_t *timer)
{
  bool greeting;
  bool checkin;
  char status[sizeof(g_product_remote_checkin_status)];

  (void)timer;
  pthread_mutex_lock(&g_product_remote_lock);
  greeting = g_product_remote_greeting_pending;
  checkin = g_product_remote_checkin_pending;
  g_product_remote_greeting_pending = false;
  g_product_remote_checkin_pending = false;
  memcpy(status, g_product_remote_checkin_status, sizeof(status));
  status[sizeof(status) - 1u] = '\0';
  pthread_mutex_unlock(&g_product_remote_lock);

  if (greeting)
    {
      start_greeting_overlay();
    }
  if (checkin)
    {
      set_choice_status(status, 0x65d48a);
    }
}

static void set_product_voice_phase(product_voice_phase_t phase)
{
  pthread_mutex_lock(&g_product_voice_lock);
  g_product_voice_phase = phase;
  pthread_mutex_unlock(&g_product_voice_lock);
}

static product_voice_phase_t product_voice_phase(void)
{
  product_voice_phase_t phase;

  pthread_mutex_lock(&g_product_voice_lock);
  phase = g_product_voice_phase;
  pthread_mutex_unlock(&g_product_voice_lock);
  return phase;
}

static bool product_voice_busy(void)
{
  product_voice_phase_t phase = product_voice_phase();
  return phase == PRODUCT_VOICE_RECORDING ||
         phase == PRODUCT_VOICE_TRANSCRIBING ||
         phase == PRODUCT_VOICE_SPEAKING ||
         phase == PRODUCT_VOICE_TRANSCRIPT_READY;
}

static void *stop_product_voice_worker(void *unused)
{
  lc_native_voice_status_t status;

  (void)unused;
  status = lc_native_voice_finish(g_product_audio, g_product_capture_ticket,
                                   g_product_voice_session,
                                   sizeof(g_product_voice_session),
                                   g_product_transcript,
                                   sizeof(g_product_transcript));
  set_product_voice_phase(status == LC_NATIVE_VOICE_OK &&
                          lc_native_voice_current(g_product_audio, g_product_capture_ticket)
                            ? PRODUCT_VOICE_TRANSCRIPT_READY
                            : PRODUCT_VOICE_TRANSCRIPT_ERROR);
  return NULL;
}

static void *speak_product_reply_worker(void *unused)
{
  lc_native_voice_status_t status;

  (void)unused;
  status = lc_native_voice_speak(g_product_audio, g_product_speech_session,
                                g_product_speech);
  memset(g_product_speech, 0, sizeof(g_product_speech));
  memset(g_product_speech_session, 0, sizeof(g_product_speech_session));
  set_product_voice_phase(status == LC_NATIVE_VOICE_OK
                            ? PRODUCT_VOICE_SPEAK_DONE
                            : PRODUCT_VOICE_SPEAK_ERROR);
  return NULL;
}

static bool begin_product_speech(const char *text)
{
  size_t length;

  if (!g_product_audio_ready || text == NULL || product_voice_busy() ||
      g_product_session[0] == '\0')
    {
      return false;
    }

  length = strlen(text);
  if (length == 0u || length >= sizeof(g_product_speech))
    {
      return false;
    }

  pthread_mutex_lock(&g_product_voice_lock);
  memcpy(g_product_speech, text, length + 1u);
  memcpy(g_product_speech_session, g_product_session,
         sizeof(g_product_speech_session));
  g_product_voice_phase = PRODUCT_VOICE_SPEAKING;
  pthread_mutex_unlock(&g_product_voice_lock);

  if (!spawn_product_voice_worker(speak_product_reply_worker))
    {
      memset(g_product_speech, 0, sizeof(g_product_speech));
      memset(g_product_speech_session, 0, sizeof(g_product_speech_session));
      set_product_voice_phase(PRODUCT_VOICE_SPEAK_ERROR);
      return false;
    }

  return true;
}

static void set_product_backend_phase(product_backend_phase_t phase,
                                      lc_product_transport_status_t status)
{
  pthread_mutex_lock(&g_product_backend_lock);
  g_product_backend_phase = phase;
  g_product_backend_status = status;
  pthread_mutex_unlock(&g_product_backend_lock);
}

static product_backend_phase_t product_backend_phase(void)
{
  product_backend_phase_t phase;

  pthread_mutex_lock(&g_product_backend_lock);
  phase = g_product_backend_phase;
  pthread_mutex_unlock(&g_product_backend_lock);
  return phase;
}

static void hide_product_result(void)
{
  if (g_product_cat_bubble != NULL)
    {
      lv_obj_add_flag(g_product_cat_bubble, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(g_product_dog_bubble, LV_OBJ_FLAG_HIDDEN);
    }
  if (g_product_panel != NULL)
    {
      lv_obj_add_flag(g_product_panel, LV_OBJ_FLAG_HIDDEN);
    }
#  if LV_USE_QRCODE
  if (g_product_qr != NULL)
    {
      lv_obj_add_flag(g_product_qr, LV_OBJ_FLAG_HIDDEN);
    }
#  endif
}

static void show_choice_cards(bool visible)
{
  unsigned int index;

  for (index = 0u; index < LC_CHOICE_COUNT; index++)
    {
      if (visible)
        {
          lv_obj_remove_flag(g_choice_cards[index], LV_OBJ_FLAG_HIDDEN);
        }
      else
        {
          lv_obj_add_flag(g_choice_cards[index], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void update_product_dialogue_visibility(void)
{
  lc_product_dialogue_t dialogue;
  uint8_t mask;

  /* Bubbles are independent screen nodes: never reveal them over a QR. */
  if (g_product_flow.state == LC_PRODUCT_QR ||
      g_product_flow.state == LC_PRODUCT_CHOICE)
    {
      return;
    }

  if (g_product_title == NULL || g_product_detail == NULL ||
      !lc_product_dialogue_build(&g_product_flow, &dialogue))
    {
      return;
    }

  mask = lc_product_dialogue_reveal_mask(
    &dialogue, monotonic_ms() - g_product_dialogue_started_ms);
  if ((mask & LC_PRODUCT_DIALOGUE_COORDINATOR_VISIBLE) != 0u)
    {
      lv_obj_remove_flag(g_product_dog_bubble, LV_OBJ_FLAG_HIDDEN);
    }
  else
    {
      lv_obj_add_flag(g_product_dog_bubble, LV_OBJ_FLAG_HIDDEN);
    }

  if ((mask & LC_PRODUCT_DIALOGUE_COMPANION_VISIBLE) != 0u)
    {
      lv_obj_remove_flag(g_product_cat_bubble, LV_OBJ_FLAG_HIDDEN);
    }
  else
    {
      lv_obj_add_flag(g_product_cat_bubble, LV_OBJ_FLAG_HIDDEN);
    }
}

static void render_product_flow(void)
{
  lc_product_dialogue_t dialogue;
  const char *source_label;

  if (g_greeting_animation.active)
    {
      finish_greeting_overlay();
    }
  hide_product_result();
  if (g_product_flow.state == LC_PRODUCT_CHOICE ||
      g_product_flow.state == LC_PRODUCT_REQUESTING ||
      g_product_flow.state == LC_PRODUCT_ERROR)
    {
      show_choice_cards(true);
    }
  else
    {
      show_choice_cards(false);
    }

  if (g_product_flow.state != LC_PRODUCT_QR &&
      lc_product_dialogue_build(&g_product_flow, &dialogue) &&
      dialogue.visible)
    {
      /* Short head-level speech; full result stays in the temporary footer. */
      switch (g_product_flow.state)
        {
          case LC_PRODUCT_REQUESTING:
            lv_label_set_text(g_product_title, "帮你选餐");
            lv_label_set_text(g_product_detail, "检查口味");
            break;
          case LC_PRODUCT_RECOMMENDATION:
            lv_label_set_text(g_product_title, "请先确认");
            lv_label_set_text(g_product_detail, g_product_flow.result.name);
            break;
          case LC_PRODUCT_CONFIRMING:
            lv_label_set_text(g_product_title, "准备二维码");
            lv_label_set_text(g_product_detail, "手机付款");
            break;
          case LC_PRODUCT_COMPLETE:
            lv_label_set_text(g_product_title, "已确认菜品");
            lv_label_set_text(g_product_detail, "在家做饭");
            break;
          case LC_PRODUCT_ERROR:
            lv_label_set_text(g_product_title,
              g_product_flow.error == LC_PRODUCT_ERROR_NO_CANDIDATE
                ? "没有合适的菜" : "服务未连通");
            lv_label_set_text(g_product_detail,
              g_product_flow.error == LC_PRODUCT_ERROR_NO_CANDIDATE
                ? "请调整条件" : "点击重试");
            break;
          default:
            lv_label_set_text(g_product_title, "服务未连通");
            lv_label_set_text(g_product_detail, "点击重试");
            break;
        }
      if (!g_product_dialogue_started ||
          g_product_dialogue_state != g_product_flow.state)
        {
          g_product_dialogue_started = true;
          g_product_dialogue_state = g_product_flow.state;
          g_product_dialogue_started_ms = monotonic_ms();
        }
      if (g_product_flow.state == LC_PRODUCT_RECOMMENDATION)
        {
          source_label = g_product_flow.result.rules_fallback
                           ? "本地规则推荐"
                           : (g_product_flow.result.source ==
                                LC_PRODUCT_SOURCE_MODEL
                                ? "AI + 规则检查" : "规则推荐");
          static char result_text[LC_PRODUCT_NAME_MAX + LC_PRODUCT_REASON_MAX + 96u];
          (void)snprintf(result_text, sizeof(result_text), "%s | %s\n%s\n%s | 点击确认",
                         g_product_flow.result.name, g_product_flow.result.price,
                         g_product_flow.result.reason, source_label);
          lv_label_set_text(g_product_badge, result_text);
        }
      else if (g_product_flow.state == LC_PRODUCT_CONFIRMING)
        {
          lv_label_set_text(g_product_badge, "正在确认，请稍候");
        }
      else if (g_product_flow.state == LC_PRODUCT_REQUESTING)
        {
          lv_label_set_text(g_product_badge, "正在分析你的需求");
        }
      else if (g_product_flow.state == LC_PRODUCT_ERROR)
        {
          lv_label_set_text(g_product_badge,
            g_product_flow.error == LC_PRODUCT_ERROR_NO_CANDIDATE
              ? "当前没有合适候选，请调整条件" : "连接未成功，可重试");
        }
      else
        {
          lv_label_set_text(g_product_badge, "已确认在家做饭");
        }

      if (g_product_flow.state == LC_PRODUCT_RECOMMENDATION ||
          g_product_flow.state == LC_PRODUCT_CONFIRMING ||
          g_product_flow.state == LC_PRODUCT_COMPLETE)
        {
          lv_obj_remove_flag(g_product_panel, LV_OBJ_FLAG_HIDDEN);
        }
      update_product_dialogue_visibility();
    }
  else if (g_product_flow.state == LC_PRODUCT_QR)
    {
#  if LV_USE_QRCODE
      if (lv_qrcode_update(g_product_qr, g_product_flow.handoff_url,
                           (uint32_t)strlen(g_product_flow.handoff_url)) ==
          LV_RESULT_OK)
        {
          lv_obj_remove_flag(g_product_qr, LV_OBJ_FLAG_HIDDEN);
          set_choice_status("扫码打开饿了么，手机付款", 0x65d48a);
        }
      else
        {
          lc_product_flow_fail(&g_product_flow,
                               LC_PRODUCT_ERROR_HANDOFF,
                               "QR generation failed", true);
          set_choice_status("二维码生成失败，请重试", 0xff8b8b);
        }
#  else
      lc_product_flow_fail(&g_product_flow, LC_PRODUCT_ERROR_HANDOFF,
                           "QR module disabled", false);
      set_choice_status("二维码模块未启用", 0xff8b8b);
#  endif
    }
  else if (g_product_flow.state == LC_PRODUCT_ERROR)
    {
      set_choice_status(g_product_flow.retryable
                          ? "连接失败，点击桌面物品重试"
                          : "服务配置未完成",
                        0xff8b8b);
    }
}

static bool ensure_product_backend_timer(void)
{
  if (g_product_timer == NULL)
    {
      g_product_timer = lv_timer_create(poll_product_backend, 100u, NULL);
    }
  else
    {
      lv_timer_resume(g_product_timer);
    }

  return g_product_timer != NULL;
}

static void *product_recommend_worker(void *unused)
{
  lc_product_transport_status_t status;

  (void)unused;
  if (g_product_session[0] != '\0')
    {
      if (g_product_backend_reject)
        status = lc_product_transport_reject_session(
          &g_product_transport, g_product_session, g_product_backend_choice,
          g_product_backend_transcript, &g_product_backend_result);
      else
        status = lc_product_transport_recommend_session(
          &g_product_transport, g_product_session, g_product_backend_choice,
          g_product_backend_transcript, &g_product_backend_result);
    }
  else
    {
      status = lc_product_transport_recommend(
        &g_product_transport, g_product_backend_choice,
        g_product_backend_transcript, &g_product_backend_result,
        g_product_session, sizeof(g_product_session));
    }
  set_product_backend_phase(status == LC_PRODUCT_TRANSPORT_OK
                              ? PRODUCT_BACKEND_RECOMMEND_READY
                              : PRODUCT_BACKEND_ERROR,
                            status);
  return NULL;
}

static void *product_confirm_worker(void *unused)
{
  lc_product_transport_status_t status;

  (void)unused;
  status = lc_product_transport_confirm(
    &g_product_transport, g_product_session,
    g_product_handoff, sizeof(g_product_handoff));
  set_product_backend_phase(status == LC_PRODUCT_TRANSPORT_OK
                              ? PRODUCT_BACKEND_HANDOFF_READY
                              : PRODUCT_BACKEND_ERROR,
                            status);
  return NULL;
}

static bool begin_product_request_action(lc_choice_id_t choice,
                                               const char *transcript,
                                               const char *existing_session,
                                               bool reject_current)
{
  size_t length;
  char session_snapshot[LC_PRODUCT_SESSION_MAX] = {0};

  if (reject_current && (existing_session == NULL ||
      g_product_flow.state != LC_PRODUCT_RECOMMENDATION)) return false;

  if (existing_session != NULL)
    {
      size_t session_length = strlen(existing_session);
      if (session_length != 17u || strncmp(existing_session, "meal_", 5u) != 0)
        {
          set_choice_status("语音会话无效，请重新说", 0xff8b8b);
          return false;
        }
      for (size_t i = 5u; i < session_length; i++)
        if (!((existing_session[i] >= '0' && existing_session[i] <= '9') ||
              (existing_session[i] >= 'a' && existing_session[i] <= 'f')))
          {
            set_choice_status("语音会话无效，请重新说", 0xff8b8b);
            return false;
          }
      /* Snapshot before mutating globals: caller may supply the old buffer. */
      memcpy(session_snapshot, existing_session, session_length + 1u);
    }

  if (!g_product_transport_ready)
    {
      g_product_transport_ready =
        lc_product_transport_init_default(&g_product_transport);
    }

  if (!g_product_transport_ready || transcript == NULL ||
      transcript[0] == '\0')
    {
      set_choice_status("服务地址未配置", 0xff8b8b);
      return false;
    }

  if (g_product_flow.state == LC_PRODUCT_ERROR &&
      !lc_product_flow_retry(&g_product_flow))
    {
      lc_product_flow_reset(&g_product_flow);
    }

  if (g_product_flow.state != LC_PRODUCT_CHOICE)
    {
      lc_product_flow_reset(&g_product_flow);
    }

  if (!lc_product_flow_start(&g_product_flow, choice))
    {
      return false;
    }

  length = strlen(transcript);
  if (length >= sizeof(g_product_backend_transcript))
    {
      lc_product_flow_fail(&g_product_flow, LC_PRODUCT_ERROR_SCHEMA,
                           "Transcript is too long", true);
      render_product_flow();
      return false;
    }

  if (!ensure_product_backend_timer())
    {
      lc_product_flow_fail(&g_product_flow, LC_PRODUCT_ERROR_NETWORK,
                           "Could not start backend poll", true);
      g_product_waiting = false;
      g_choice_interactive = true;
      render_product_flow();
      return false;
    }

  pthread_mutex_lock(&g_product_backend_lock);
  g_product_backend_choice = choice;
  g_product_backend_reject = reject_current;
  memcpy(g_product_backend_transcript, transcript, length + 1u);
  memset(&g_product_backend_result, 0, sizeof(g_product_backend_result));
  memcpy(g_product_session, session_snapshot, sizeof(g_product_session));
  g_product_backend_phase = PRODUCT_BACKEND_RECOMMENDING;
  g_product_backend_status = LC_PRODUCT_TRANSPORT_OK;
  pthread_mutex_unlock(&g_product_backend_lock);

  g_product_waiting = true;
  g_choice_interactive = false;
  render_product_flow();
  set_choice_status("正在思考", 0xc8b9ff);
  if (!spawn_product_voice_worker(product_recommend_worker))
    {
      lc_product_flow_fail(&g_product_flow, LC_PRODUCT_ERROR_NETWORK,
                           "Could not start backend request", true);
      g_product_waiting = false;
      g_choice_interactive = true;
      lv_timer_pause(g_product_timer);
      render_product_flow();
      return false;
    }

  return true;
}

static bool begin_product_request_with_session(lc_choice_id_t choice,
  const char *transcript, const char *existing_session)
{
  return begin_product_request_action(choice, transcript, existing_session, false);
}

static bool begin_product_request(lc_choice_id_t choice, const char *transcript)
{
  /* Touch entry has no ASR session; retain existing behavior. */
  return begin_product_request_with_session(choice, transcript, NULL);
}

static void begin_product_confirmation(void)
{
  if (!lc_product_flow_begin_confirmation(&g_product_flow))
    {
      return;
    }

  render_product_flow();
  if (!g_product_flow.result.requires_handoff)
    {
      (void)lc_product_flow_complete_local(&g_product_flow);
      render_product_flow();
      set_choice_status("在家做饭方案已就绪", 0x65d48a);
      return;
    }

  if (!ensure_product_backend_timer())
    {
      lc_product_flow_fail(&g_product_flow, LC_PRODUCT_ERROR_NETWORK,
                           "Could not start backend poll", true);
      g_product_waiting = false;
      g_choice_interactive = true;
      render_product_flow();
      return;
    }

  g_product_waiting = true;
  set_product_backend_phase(PRODUCT_BACKEND_CONFIRMING,
                            LC_PRODUCT_TRANSPORT_OK);
  if (!spawn_product_voice_worker(product_confirm_worker))
    {
      lc_product_flow_fail(&g_product_flow, LC_PRODUCT_ERROR_HANDOFF,
                           "Could not start confirmation", true);
      g_product_waiting = false;
      g_choice_interactive = true;
      lv_timer_pause(g_product_timer);
      render_product_flow();
      return;
    }
}

static void product_result_event_cb(lv_event_t *event)
{
  if (lv_event_get_code(event) == LV_EVENT_CLICKED && !g_product_waiting &&
      !product_voice_busy() &&
      g_product_flow.state == LC_PRODUCT_RECOMMENDATION)
    {
      begin_product_confirmation();
    }
}

static void poll_product_backend(lv_timer_t *timer)
{
  product_backend_phase_t phase = product_backend_phase();

  if (phase == PRODUCT_BACKEND_RECOMMEND_READY)
    {
      bool speaking = false;

      if (!lc_product_flow_set_recommendation(&g_product_flow,
                                               &g_product_backend_result))
        {
          lc_product_flow_fail(&g_product_flow, LC_PRODUCT_ERROR_SCHEMA,
                               "Invalid recommendation", true);
        }
      else
        {
          char short_reply[LC_PRODUCT_NAME_MAX + 32u];
          (void)snprintf(short_reply, sizeof(short_reply), "要试试%s吗？",
                         g_product_flow.result.name);
          speaking = begin_product_speech(short_reply);
        }

      g_product_waiting = false;
      g_choice_interactive = true;
      set_product_backend_phase(PRODUCT_BACKEND_IDLE,
                                LC_PRODUCT_TRANSPORT_OK);
      lv_timer_pause(timer);
      render_product_flow();
      set_choice_status(g_product_flow.state == LC_PRODUCT_RECOMMENDATION
                          ? (speaking ? "正在回答"
                                      : "推荐已就绪，请确认")
                          : "结果无效，请重试",
                        g_product_flow.state == LC_PRODUCT_RECOMMENDATION
                          ? 0x65d48a : 0xff8b8b);
    }
  else if (phase == PRODUCT_BACKEND_HANDOFF_READY)
    {
      if (!lc_product_flow_set_handoff(&g_product_flow,
                                       g_product_handoff))
        {
          lc_product_flow_fail(&g_product_flow, LC_PRODUCT_ERROR_HANDOFF,
                               "Invalid handoff", true);
        }

      g_product_waiting = false;
      g_choice_interactive = true;
      set_product_backend_phase(PRODUCT_BACKEND_IDLE,
                                LC_PRODUCT_TRANSPORT_OK);
      lv_timer_pause(timer);
      render_product_flow();
    }
  else if (phase == PRODUCT_BACKEND_ERROR)
    {
      lc_product_error_t error =
        g_product_backend_status == LC_PRODUCT_TRANSPORT_NO_CANDIDATE
          ? LC_PRODUCT_ERROR_NO_CANDIDATE
          : (g_product_backend_status == LC_PRODUCT_TRANSPORT_SCHEMA
              ? LC_PRODUCT_ERROR_SCHEMA : LC_PRODUCT_ERROR_NETWORK);
      lc_product_flow_fail(&g_product_flow, error,
                           "Decision service unavailable", true);
      g_product_waiting = false;
      g_choice_interactive = true;
      set_product_backend_phase(PRODUCT_BACKEND_IDLE,
                                g_product_backend_status);
      lv_timer_pause(timer);
      render_product_flow();
      if (error == LC_PRODUCT_ERROR_NO_CANDIDATE)
        set_choice_status("没有合适的菜，请调整条件", 0xffd66b);
    }
}

static bool stop_product_voice_async(void)
{
  if (product_voice_phase() != PRODUCT_VOICE_RECORDING)
    {
      return false;
    }

  set_product_voice_phase(PRODUCT_VOICE_TRANSCRIBING);
  set_choice_status("正在识别语音", 0xc8b9ff);
  if (!spawn_product_voice_worker(stop_product_voice_worker))
    {
      set_product_voice_phase(PRODUCT_VOICE_TRANSCRIPT_ERROR);
      return false;
    }

  return true;
}

static void product_voice_event_cb(lv_event_t *event)
{
  lc_native_voice_status_t status;
  product_voice_phase_t phase;

  if (!g_product_mode ||
      lv_event_get_code(event) != LV_EVENT_CLICKED || g_product_waiting)
    {
      return;
    }

  if (!g_product_audio_ready)
    {
      set_choice_status("语音未配置，触控可用", 0xffd66b);
      return;
    }

  phase = product_voice_phase();
  if (phase == PRODUCT_VOICE_RECORDING)
    {
      (void)stop_product_voice_async();
      return;
    }

  if (phase != PRODUCT_VOICE_IDLE &&
      phase != PRODUCT_VOICE_TRANSCRIPT_ERROR &&
      phase != PRODUCT_VOICE_SPEAK_DONE &&
      phase != PRODUCT_VOICE_SPEAK_ERROR)
    {
      return;
    }

  memset(g_product_transcript, 0, sizeof(g_product_transcript));
  memset(g_product_voice_session, 0, sizeof(g_product_voice_session));
  if (g_greeting_animation.active)
    {
      finish_greeting_overlay();
    }
  status = lc_native_voice_start(g_product_audio,
    g_product_session[0] != '\0' &&
      (g_product_flow.state == LC_PRODUCT_RECOMMENDATION ||
       g_product_flow.state == LC_PRODUCT_ERROR) ? g_product_session : NULL,
    &g_product_capture_ticket);
  if (status != LC_NATIVE_VOICE_OK)
    {
      set_product_voice_phase(PRODUCT_VOICE_TRANSCRIPT_ERROR);
      set_choice_status("麦克风未就绪，请重试", 0xff8b8b);
      return;
    }

  g_product_voice_started_ms = monotonic_ms();
  set_product_voice_phase(PRODUCT_VOICE_RECORDING);
  set_choice_status("正在聆听，再点发送", 0xc8b9ff);
}

static void poll_product_voice(lv_timer_t *timer)
{
  product_voice_phase_t phase = product_voice_phase();

  (void)timer;
  if (g_product_dialogue_started)
    {
      update_product_dialogue_visibility();
    }
  if (phase == PRODUCT_VOICE_RECORDING &&
      monotonic_ms() - g_product_voice_started_ms >= PRODUCT_VOICE_MAX_MS)
    {
      (void)stop_product_voice_async();
      return;
    }

  if (phase == PRODUCT_VOICE_TRANSCRIPT_READY)
    {
      char transcript[LC_PRODUCT_TRANSCRIPT_BYTES];
      char session[LC_PRODUCT_SESSION_MAX];
      lc_choice_id_t choice;
      lc_product_voice_action_t action;

      pthread_mutex_lock(&g_product_voice_lock);
      memcpy(transcript, g_product_transcript, sizeof(transcript));
      memcpy(session, g_product_voice_session, sizeof(session));
      memset(g_product_transcript, 0, sizeof(g_product_transcript));
      memset(g_product_voice_session, 0, sizeof(g_product_voice_session));
      g_product_voice_phase = PRODUCT_VOICE_IDLE;
      pthread_mutex_unlock(&g_product_voice_lock);
      lc_choice_id_t selected = (g_product_flow.state == LC_PRODUCT_RECOMMENDATION ||
                                g_product_flow.state == LC_PRODUCT_ERROR)
        ? g_product_flow.choice : ((g_choice.armed || g_choice.confirmed)
                                   ? g_choice.selected : LC_CHOICE_TAKEOUT);
      action = lc_product_intent_route(transcript, selected,
        g_product_flow.state == LC_PRODUCT_RECOMMENDATION, &choice);
      if (!lc_native_voice_current(g_product_audio, g_product_capture_ticket))
        {
          set_choice_status("本次语音已取消，请重试", 0xffd66b);
        }
      else if (action == LC_PRODUCT_VOICE_CONFIRM)
        {
          if (strcmp(session, g_product_session) == 0)
            begin_product_confirmation();
          else
            set_choice_status("会话已变化，请重新确认", 0xffd66b);
        }
      else if (action == LC_PRODUCT_VOICE_REJECT)
        {
          if (strcmp(session, g_product_session) != 0 ||
              !begin_product_request_action(choice, transcript, session, true))
            set_choice_status("换菜未完成，请重试", 0xff8b8b);
        }
      else if (action == LC_PRODUCT_VOICE_UNRESOLVED)
        {
          set_choice_status("请说口味、换一个或明确确认", 0xffd66b);
        }
      else if (!begin_product_request_with_session(choice, transcript, session))
        {
          set_choice_status("服务未连通，请重试", 0xff8b8b);
        }
      memset(transcript, 0, sizeof(transcript));
      memset(session, 0, sizeof(session));
    }
  else if (phase == PRODUCT_VOICE_TRANSCRIPT_ERROR)
    {
      set_choice_status("没有听清，请再说一次", 0xffd66b);
    }
  else if (phase == PRODUCT_VOICE_SPEAK_DONE)
    {
      if (g_product_flow.state == LC_PRODUCT_RECOMMENDATION)
        {
          set_choice_status("推荐已就绪，请确认", 0x65d48a);
        }
      else if (g_product_flow.state == LC_PRODUCT_QR)
        {
          set_choice_status("扫码打开饿了么，手机付款", 0x65d48a);
        }
      else
        {
          set_choice_status("点击再次说话", 0x65d48a);
        }
      set_product_voice_phase(PRODUCT_VOICE_IDLE);
    }
  else if (phase == PRODUCT_VOICE_SPEAK_ERROR)
    {
      if (g_product_flow.state == LC_PRODUCT_RECOMMENDATION)
        {
          set_choice_status("推荐已就绪，语音暂不可用", 0xffd66b);
        }
      set_product_voice_phase(PRODUCT_VOICE_IDLE);
    }
}

static void choice_card_event_cb(lv_event_t *event)
{
  const lc_choice_id_t *choice = lv_event_get_user_data(event);
  lc_choice_tap_result_t result;

  if (!g_choice_interactive || (g_product_mode && product_voice_busy()) ||
      lv_event_get_code(event) != LV_EVENT_CLICKED ||
      choice == NULL)
    {
      return;
    }

  /*
   * A retryable transport failure leaves the cards visible under SAFE STOP.
   * Treat the next card tap as an explicit retry: return to the choice state
   * first, then let the normal two-tap confirmation logic handle that tap.
   */
  if (g_product_mode &&
      g_product_flow.state == LC_PRODUCT_ERROR &&
      g_product_flow.retryable)
    {
      if (!lc_product_flow_retry(&g_product_flow))
        {
          return;
        }

      lc_choice_init(&g_choice);
      render_product_flow();
    }

  result = lc_choice_tap(&g_choice, *choice);
  if (result != LC_CHOICE_TAP_INVALID)
    {
      render_choice_state();
      if (g_product_mode && result == LC_CHOICE_TAP_CONFIRMED)
        {
          static const char *const touch_requests[LC_CHOICE_COUNT] =
          {
            "帮我点外卖",
            "帮我打开口味盲盒",
            "我想在家做饭"
          };

          (void)begin_product_request(*choice, touch_requests[*choice]);
        }
    }
}

static bool create_choice_overlay(lv_display_t *display,
                                  lv_obj_t *screen,
                                  bool interactive)
{
  static lv_image_dsc_t card_images[LC_CHOICE_COUNT];
  lc_ui_choice_layout_t layout;
  int32_t spacing;

  if (!lc_ui_compute_choice_layout(
        (unsigned int)lv_display_get_horizontal_resolution(display),
        (unsigned int)lv_display_get_vertical_resolution(display), &layout))
    {
      return false;
    }

  spacing = (int32_t)layout.card_spacing;

  lc_choice_init(&g_choice);
  g_choice_interactive = interactive;
  g_choice_status = NULL;
  init_image_descriptor(&card_images[LC_CHOICE_TAKEOUT], lc_card_takeout,
                        LC_CARD_BYTES, LC_CARD_WIDTH, LC_CARD_HEIGHT);
  init_image_descriptor(&card_images[LC_CHOICE_MYSTERY], lc_card_mystery,
                        LC_CARD_BYTES, LC_CARD_WIDTH, LC_CARD_HEIGHT);
  init_image_descriptor(&card_images[LC_CHOICE_HOME], lc_card_home,
                        LC_CARD_BYTES, LC_CARD_WIDTH, LC_CARD_HEIGHT);

  if (g_product_mode)
    {
      unsigned int index;
      for (index = 0u; index < LC_CHOICE_COUNT; index++)
        {
          g_choice_cards[index] =
            create_room_hotspot(screen, (lc_choice_id_t)index, interactive);
        }
    }
  else
    {
  g_choice_cards[LC_CHOICE_TAKEOUT] =
    create_choice_card(screen, &card_images[LC_CHOICE_TAKEOUT], -spacing,
                       &layout,
                       LC_CHOICE_TAKEOUT, interactive);
  g_choice_cards[LC_CHOICE_MYSTERY] =
    create_choice_card(screen, &card_images[LC_CHOICE_MYSTERY], 0,
                       &layout,
                       LC_CHOICE_MYSTERY, interactive);
  g_choice_cards[LC_CHOICE_HOME] =
    create_choice_card(screen, &card_images[LC_CHOICE_HOME], spacing,
                       &layout,
                       LC_CHOICE_HOME, interactive);
    }

  if (interactive)
    {
      g_choice_status = lv_label_create(screen);
      configure_label(g_choice_status, 0xffffff);
      lv_obj_set_style_bg_color(g_choice_status, lv_color_hex(0x24150f), 0);
      lv_obj_set_style_bg_opa(g_choice_status, LV_OPA_70, 0);
      lv_obj_set_style_pad_all(g_choice_status, 5, 0);
      lv_obj_set_style_radius(g_choice_status, 8, 0);
      lv_obj_align(g_choice_status, LV_ALIGN_TOP_MID, 0,
                   (int32_t)layout.status_top);
      render_choice_state();
      if (g_product_mode)
        {
          lv_obj_add_flag(g_choice_status, LV_OBJ_FLAG_CLICKABLE);
          lv_obj_add_event_cb(g_choice_status, product_voice_event_cb,
                              LV_EVENT_CLICKED, NULL);
          set_choice_status("点击开始说话", 0xffffff);
        }
    }

  return true;
}

static lv_obj_t *create_room_bubble(lv_obj_t *screen,
                                   const lc_ui_rect_t *rect,
                                   lv_image_dsc_t *source,
                                   lv_obj_t **label)
{
  return lc_room_bubble_create(screen, rect, source, g_product_room.scale,
    &lc_font_cjk_14, label, product_result_event_cb, NULL);
}

static void create_product_overlay(lv_obj_t *screen)
{
  static lv_image_dsc_t bubbles[2];
  unsigned int index;
  memset(bubbles, 0, sizeof(bubbles));
  for (index = 0u; index < 2u; index++)
    {
      bubbles[index].header.magic = LV_IMAGE_HEADER_MAGIC;
      bubbles[index].header.cf = LV_COLOR_FORMAT_ARGB8888;
      bubbles[index].header.w = LC_BUBBLE_WIDTH;
      bubbles[index].header.h = LC_BUBBLE_HEIGHT;
      bubbles[index].header.stride = LC_BUBBLE_WIDTH * 4u;
      bubbles[index].data_size = LC_BUBBLE_BYTES;
    }
  bubbles[0].data = lc_bubble_cat;
  bubbles[1].data = lc_bubble_dog;
  g_product_cat_bubble = create_room_bubble(screen, &g_product_room.cat_bubble,
                                           &bubbles[0], &g_product_detail);
  g_product_dog_bubble = create_room_bubble(screen, &g_product_room.dog_bubble,
                                           &bubbles[1], &g_product_title);
  g_product_panel = lv_obj_create(screen);
  lv_obj_remove_style_all(g_product_panel);
  lv_obj_set_size(g_product_panel, g_product_room.result.width,
                  g_product_room.result.height);
  lv_obj_set_pos(g_product_panel, g_product_room.result.x,
                 g_product_room.result.y);
  lv_obj_set_style_bg_color(g_product_panel, lv_color_hex(0x24150f), 0);
  lv_obj_set_style_bg_opa(g_product_panel, LV_OPA_80, 0);
  lv_obj_set_style_border_color(g_product_panel, lv_color_hex(0x65d48a), 0);
  lv_obj_set_style_border_width(g_product_panel, 2, 0);
  lv_obj_set_style_radius(g_product_panel, 14, 0);
  lv_obj_add_flag(g_product_panel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(g_product_panel, product_result_event_cb,
                      LV_EVENT_CLICKED, NULL);

  g_product_badge = lv_label_create(g_product_panel);
  configure_label(g_product_badge, 0x65d48a);
  lv_obj_set_width(g_product_badge, g_product_room.result.width - 12u);
  lv_obj_align(g_product_badge, LV_ALIGN_TOP_MID, 0, 4);
  lv_obj_remove_flag(g_product_badge, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(g_product_panel, LV_OBJ_FLAG_HIDDEN);

#  if LV_USE_QRCODE
  g_product_qr = lv_qrcode_create(screen);
  lv_qrcode_set_size(g_product_qr, 112u * g_product_room.scale / 256u);
  lv_qrcode_set_dark_color(g_product_qr, lv_color_hex(0x1d130f));
  lv_qrcode_set_light_color(g_product_qr, lv_color_hex(0xfff7e8));
  lv_obj_set_pos(g_product_qr,
                 g_product_room.artwork.x + 64u * g_product_room.scale / 256u,
                 g_product_room.artwork.y);
  lv_obj_add_flag(g_product_qr, LV_OBJ_FLAG_HIDDEN);
#  endif
}

static void set_competition_text(const char *title,
                                 const char *detail,
                                 const char *badge)
{
  lv_label_set_text(g_competition_title, title);
  lv_label_set_text(g_competition_detail, detail);
  lv_label_set_text(g_competition_badge, badge);
}

static void render_competition_state(void)
{
  unsigned int index;

#  if LV_USE_QRCODE
  if (g_competition_qr != NULL)
    {
      lv_obj_add_flag(g_competition_qr, LV_OBJ_FLAG_HIDDEN);
    }
#  endif

  for (index = 0u; index < LC_CHOICE_COUNT; index++)
    {
      if (g_choice_cards[index] != NULL)
        {
          lv_obj_remove_flag(g_choice_cards[index], LV_OBJ_FLAG_HIDDEN);
        }
    }

  switch (g_competition.state)
    {
      case LC_COMPETITION_CHOICE:
        set_competition_text("今天想吃什么？",
                             "可以开始选择外卖",
                             "点击外卖袋，帮你选餐");
        break;

      case LC_COMPETITION_REQUESTING:
        set_competition_text("比格犬正在协调",
                             "正在挑选晚餐",
                             "AI + 规则检查");
        break;

      case LC_COMPETITION_RECOMMENDATION:
        (void)snprintf(g_competition_detail_text,
                       sizeof(g_competition_detail_text), "%s | %s",
                       g_competition.reason, g_competition.price);
        set_competition_text(g_competition.dish,
                             g_competition_detail_text,
                             g_competition.rules_fallback
                               ? "规则推荐"
                               : "口味匹配推荐");
        break;

      case LC_COMPETITION_CONFIRMING:
        if (g_project_demo)
          {
            set_competition_text("演示完成",
                                 "正在准备项目介绍",
                                 "公开项目链接");
          }
        else
          {
            set_competition_text("画框已确认",
                                 "正在准备手机跳转",
                                 "由你在手机确认并付款");
          }
        break;

      case LC_COMPETITION_QR:
        for (index = 0u; index < LC_CHOICE_COUNT; index++)
          {
            lv_obj_add_flag(g_choice_cards[index], LV_OBJ_FLAG_HIDDEN);
          }
#  if LV_USE_QRCODE
        if (lv_qrcode_update(g_competition_qr, g_competition.qr_url,
                             (uint32_t)strlen(g_competition.qr_url)) !=
            LV_RESULT_OK)
          {
            lc_competition_fail(&g_competition, "QR generation failed");
            set_competition_text("暂时无法跳转",
                                 "二维码生成失败", "请重试");
            break;
          }

        lv_obj_remove_flag(g_competition_qr, LV_OBJ_FLAG_HIDDEN);
        if (g_project_demo)
          {
            set_competition_text("在手机查看项目",
                                 "打开公开项目页面",
                                 "公开项目链接");
          }
        else
          {
            set_competition_text("扫码到手机",
                                 "在手机核对下单内容",
                                 "由你确认并付款");
          }
#  else
        lc_competition_fail(&g_competition, "QR module is disabled");
        set_competition_text("暂时无法跳转", "二维码模块未启用",
                             "请检查配置");
#  endif
        break;

      case LC_COMPETITION_ERROR:
        set_competition_text("暂时无法跳转",
                             "请检查画间服务连接后重试",
                             "未提交订单");
        break;
    }
}

static void advance_competition_demo(lv_timer_t *timer)
{
  g_competition_step++;

  if (g_competition_step == 1u)
    {
      if (!lc_competition_start(&g_competition, LC_CHOICE_TAKEOUT))
        {
          lc_competition_fail(&g_competition, "could not start");
        }

    }
  else if (g_competition_step == 2u)
    {
      if (!lc_competition_set_recommendation(
            &g_competition, "Tomato beef rice",
            "Warm, balanced and within budget", "About CNY 32", false))
        {
          lc_competition_fail(&g_competition, "invalid recommendation");
        }

    }
  else if (g_competition_step == 3u)
    {
      if (!lc_competition_confirm(&g_competition))
        {
          lc_competition_fail(&g_competition, "could not confirm");
        }

    }
  else if (g_competition_step == 4u)
    {
      if (!lc_competition_set_handoff(&g_competition,
                                      g_competition_handoff))
        {
          lc_competition_fail(&g_competition, "invalid handoff URL");
        }

      lv_timer_pause(timer);
    }

  render_competition_state();
}

static void create_competition_overlay(lv_obj_t *screen,
                                       const char *handoff_url,
                                       bool project_demo)
{
  size_t length = strlen(handoff_url);

  lc_competition_init(&g_competition);
  g_competition_step = 0u;
  g_project_demo = project_demo;
  memcpy(g_competition_handoff, handoff_url, length + 1u);

  g_competition_panel = lv_obj_create(screen);
  lv_obj_remove_style_all(g_competition_panel);
  lv_obj_set_size(g_competition_panel, LV_PCT(92), 112);
  lv_obj_align(g_competition_panel, LV_ALIGN_TOP_MID, 0, 8);
  lv_obj_set_style_bg_color(g_competition_panel, lv_color_hex(0x24150f), 0);
  lv_obj_set_style_bg_opa(g_competition_panel, LV_OPA_80, 0);
  lv_obj_set_style_border_color(g_competition_panel,
                                lv_color_hex(0xffd07b), 0);
  lv_obj_set_style_border_width(g_competition_panel, 2, 0);
  lv_obj_set_style_radius(g_competition_panel, 14, 0);

  g_competition_title = lv_label_create(g_competition_panel);
  configure_label(g_competition_title, 0xffffff);
  lv_obj_align(g_competition_title, LV_ALIGN_TOP_MID, 0, 12);

  g_competition_detail = lv_label_create(g_competition_panel);
  configure_label(g_competition_detail, 0xffe9c8);
  lv_obj_align(g_competition_detail, LV_ALIGN_CENTER, 0, 4);

  g_competition_badge = lv_label_create(g_competition_panel);
  configure_label(g_competition_badge, 0xffd26a);
  lv_obj_align(g_competition_badge, LV_ALIGN_BOTTOM_MID, 0, -10);

#  if LV_USE_QRCODE
  g_competition_qr = lv_qrcode_create(screen);
  lv_qrcode_set_size(g_competition_qr, 124);
  lv_qrcode_set_dark_color(g_competition_qr, lv_color_hex(0x1d130f));
  lv_qrcode_set_light_color(g_competition_qr, lv_color_hex(0xfff7e8));
  lv_obj_align(g_competition_qr, LV_ALIGN_CENTER, 0, 25);
  lv_obj_add_flag(g_competition_qr, LV_OBJ_FLAG_HIDDEN);
#  endif

  render_competition_state();
  g_competition_timer = lv_timer_create(advance_competition_demo, 1800u, NULL);
}

static int run_artwork_preview(bool with_choices,
                               bool product_mode,
                               bool greeting_preview,
                               const char *presence_event_path,
                               lc_presence_source_t presence_source,
                               const char *checkin_event_path)
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
  lv_obj_t *screen;

  if (lv_is_initialized())
    {
      fprintf(stderr, "Living Canvas display is already in use\n");
      return 2;
    }

  g_product_mode = product_mode;
  g_product_waiting = false;
  g_product_timer = NULL;
  g_product_voice_timer = NULL;
  g_product_audio_ready = false;
  lc_product_transport_clear(&g_product_transport);
  g_product_transport_ready = false;
  g_product_panel = NULL;
  g_product_title = NULL;
  g_product_detail = NULL;
  g_product_badge = NULL;
  g_product_cat_bubble = NULL;
  g_product_dog_bubble = NULL;
  g_product_dialogue_started = false;
  g_product_dialogue_state = LC_PRODUCT_CHOICE;
  g_product_dialogue_started_ms = 0u;
  g_greeting_panel = NULL;
  g_greeting_message = NULL;
  g_greeting_timer = NULL;
  g_product_presence_ready = false;
  g_product_presence_pending = false;
  g_product_presence_error = false;
  g_product_presence_error_shown = false;
  g_product_presence_path[0] = '\0';
  g_product_presence_timer = NULL;
  g_product_checkin_ready = false;
  g_product_checkin_runtime_ready = false;
  g_product_checkin_pending = false;
  g_product_checkin_error = false;
  g_product_checkin_error_shown = false;
  g_product_checkin_path[0] = '\0';
  g_product_checkin_status[0] = '\0';
  g_product_checkin_timer = NULL;
  memset(&g_product_local_accessory_server, 0,
         sizeof(g_product_local_accessory_server));
  g_product_local_accessory_ready = false;
  g_product_local_accessory_error = false;
  g_product_local_accessory_error_shown = false;
  lc_checkin_feedback_init(&g_product_checkin_feedback,
                           PRODUCT_LOCAL_FEEDBACK_MS);
  g_product_local_chime_identity = LC_CHECKIN_FEEDBACK_NONE;
  g_product_local_chime_running = false;
  memset(&g_product_remote_client, 0, sizeof(g_product_remote_client));
  memset(&g_product_remote_sync, 0, sizeof(g_product_remote_sync));
  g_product_remote_ready = false;
  g_product_remote_greeting_pending = false;
  g_product_remote_checkin_pending = false;
  g_product_remote_finish_pending = false;
  g_product_remote_checkin_status[0] = '\0';
  g_product_remote_timer = NULL;
#  if LV_USE_QRCODE
  g_product_qr = NULL;
#  endif

  if (product_mode)
    {
      g_product_transport_ready =
        lc_product_transport_init_default(&g_product_transport);
      g_product_audio = g_product_transport_ready
        ? lc_native_voice_create_default(&g_product_transport, PRODUCT_VOICE_MAX_MS)
        : NULL;
      g_product_audio_ready = g_product_audio != NULL;
      g_product_capture_ticket = 0u;
      memset(g_product_voice_session, 0, sizeof(g_product_voice_session));
      memset(g_product_speech_session, 0, sizeof(g_product_speech_session));
      g_product_voice_started_ms = 0u;
      memset(g_product_transcript, 0, sizeof(g_product_transcript));
      memset(g_product_speech, 0, sizeof(g_product_speech));
      memset(g_product_session, 0, sizeof(g_product_session));
      memset(g_product_handoff, 0, sizeof(g_product_handoff));
      memset(g_product_backend_transcript, 0,
             sizeof(g_product_backend_transcript));
      memset(&g_product_backend_result, 0,
             sizeof(g_product_backend_result));
      lc_product_flow_init(&g_product_flow);
      g_product_checkin_runtime_ready =
        lc_checkin_runtime_init(&g_product_checkin_runtime) &&
        lc_checkin_format_status(&g_product_checkin_runtime.tracker,
                                 g_product_checkin_status,
                                 sizeof(g_product_checkin_status));
      set_product_backend_phase(PRODUCT_BACKEND_IDLE,
                                g_product_transport_ready
                                  ? LC_PRODUCT_TRANSPORT_OK
                                  : LC_PRODUCT_TRANSPORT_NOT_CONFIGURED);
      set_product_voice_phase(PRODUCT_VOICE_IDLE);
    }

#  ifdef NEED_BOARDINIT
  boardctl(BOARDIOC_INIT, 0);
#  endif

  lv_init();
  if (product_mode)
    {
      g_product_voice_timer = lv_timer_create(poll_product_voice, 100u, NULL);
      if (g_product_voice_timer == NULL)
        {
          fprintf(stderr, "Living Canvas voice timer init failed\n");
          lv_deinit();
          return 2;
        }
    }
  lv_nuttx_dsc_init(&info);

#  ifdef CONFIG_LV_USE_NUTTX_LCD
  info.fb_path = "/dev/lcd0";
#  endif

  lv_nuttx_init(&info, &result);
  if (result.disp == NULL)
    {
      fprintf(stderr, "Living Canvas could not open /dev/lcd0\n");
      lv_nuttx_deinit(&result);
      lv_deinit();
      return 2;
    }

  screen = lv_screen_active();
  if (!create_background(result.disp, screen))
    {
      lv_nuttx_deinit(&result);
      lv_deinit();
      return 2;
    }

  if (greeting_preview && !create_greeting_overlay(screen, true))
    {
      fprintf(stderr, "Living Canvas greeting timer init failed\n");
      lv_nuttx_deinit(&result);
      lv_deinit();
      return 2;
    }

  if (with_choices)
    {
      if (result.indev == NULL)
        {
          fprintf(stderr,
                  "Living Canvas interactive preview requires a touch input device\n");
          lv_nuttx_deinit(&result);
          lv_deinit();
          return 2;
        }

      configure_portrait_touch(&result);
      if (!create_choice_overlay(result.disp, screen, true))
        {
          lv_nuttx_deinit(&result);
          lv_deinit();
          return 2;
        }

      if (product_mode)
        {
          create_product_overlay(screen);
          render_product_flow();
          if (presence_event_path != NULL)
            {
              if (!create_greeting_overlay(screen, false))
                {
                  fprintf(stderr,
                          "Living Canvas greeting timer init failed\n");
                  lv_nuttx_deinit(&result);
                  lv_deinit();
                  return 2;
                }

              g_product_presence_timer =
                lv_timer_create(poll_product_presence, 80u, NULL);
              if (g_product_presence_timer == NULL ||
                  !start_product_presence_reader(presence_event_path,
                                                 presence_source))
                {
                  fprintf(stderr,
                          "Living Canvas presence reader init failed\n");
                  lv_nuttx_deinit(&result);
                  lv_deinit();
                  return 2;
                }
            }
          g_product_checkin_timer =
            lv_timer_create(poll_product_checkin, 80u, NULL);
          if (g_product_checkin_timer == NULL)
            {
              fprintf(stderr,
                      "Living Canvas check-in timer init failed\n");
              lv_nuttx_deinit(&result);
              lv_deinit();
              return 2;
            }
          if (checkin_event_path != NULL)
            {
              if (!start_product_checkin_reader(checkin_event_path))
                {
                  fprintf(stderr,
                          "Living Canvas check-in reader init failed\n");
                  lv_nuttx_deinit(&result);
                  lv_deinit();
                  return 2;
                }
            }
          (void)start_product_local_accessory_server();
          if (presence_event_path == NULL && checkin_event_path == NULL &&
              g_product_transport_ready)
            {
              if (!create_greeting_overlay(screen, false))
                {
                  fprintf(stderr,
                          "Living Canvas greeting timer init failed\n");
                  lv_nuttx_deinit(&result);
                  lv_deinit();
                  return 2;
                }

              if (start_product_remote_accessories())
                {
                  g_product_remote_timer =
                    lv_timer_create(poll_product_remote, 80u, NULL);
                  if (g_product_remote_timer == NULL)
                    {
                      fprintf(stderr,
                              "Living Canvas accessory timer init failed\n");
                      lv_nuttx_deinit(&result);
                      lv_deinit();
                      return 2;
                    }
                }
            }
          if (!g_product_transport_ready)
            {
              set_choice_status("服务地址未配置", 0xff8b8b);
            }
          else if (!g_product_audio_ready)
            {
              set_choice_status("语音未配置，触控可用", 0xffd66b);
            }
        }
    }

  for (;;)
    {
      uint32_t idle = lv_timer_handler();

      if (idle == 0u)
        {
          idle = 1u;
        }
      else if (idle > 50u)
        {
          idle = 50u;
        }

      usleep(idle * 1000u);
    }
}

int lc_display_run_preview(void)
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;

  if (lv_is_initialized())
    {
      fprintf(stderr, "Living Canvas display is already in use\n");
      return 2;
    }

#  ifdef NEED_BOARDINIT
  boardctl(BOARDIOC_INIT, 0);
#  endif

  lv_init();
  lv_nuttx_dsc_init(&info);

#  ifdef CONFIG_LV_USE_NUTTX_LCD
  info.fb_path = "/dev/lcd0";
#  endif

  lv_nuttx_init(&info, &result);
  if (result.disp == NULL)
    {
      fprintf(stderr, "Living Canvas could not open /dev/lcd0\n");
      lv_nuttx_deinit(&result);
      lv_deinit();
      return 2;
    }

  create_preview();

  for (;;)
    {
      uint32_t idle = lv_timer_handler();

      if (idle == 0u)
        {
          idle = 1u;
        }
      else if (idle > 50u)
        {
          idle = 50u;
        }

      usleep(idle * 1000u);
    }
}

int lc_display_run_image_preview(void)
{
  return run_artwork_preview(false, false, false, NULL,
                             LC_PRESENCE_SOURCE_ACCESSORY, NULL);
}

int lc_display_run_greeting_preview(void)
{
  return run_artwork_preview(false, false, true, NULL,
                             LC_PRESENCE_SOURCE_ACCESSORY, NULL);
}

int lc_display_run_choice_preview(void)
{
  return run_artwork_preview(true, false, false, NULL,
                             LC_PRESENCE_SOURCE_ACCESSORY, NULL);
}

int lc_display_run_product(void)
{
  return run_artwork_preview(true, true, false, NULL,
                             LC_PRESENCE_SOURCE_ACCESSORY, NULL);
}

int lc_display_run_product_accessory(const char *event_path,
                                     lc_presence_source_t source)
{
  return run_artwork_preview(true, true, false, event_path, source, NULL);
}

int lc_display_run_product_inputs(const char *presence_event_path,
                                  lc_presence_source_t source,
                                  const char *checkin_event_path)
{
  return run_artwork_preview(true, true, false, presence_event_path, source,
                             checkin_event_path);
}

static int run_link_demo(const char *qr_url, bool project_demo)
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
  lv_obj_t *screen;
  size_t length;

  if (qr_url == NULL ||
      (strncmp(qr_url, "http://", 7u) != 0 &&
       strncmp(qr_url, "https://", 8u) != 0))
    {
      fprintf(stderr, "Living Canvas requires an HTTP link URL\n");
      return 2;
    }

  length = strlen(qr_url);
  if (length == 0u || length >= sizeof(g_competition_handoff))
    {
      fprintf(stderr, "Living Canvas link URL is too long\n");
      return 2;
    }

  if (lv_is_initialized())
    {
      fprintf(stderr, "Living Canvas display is already in use\n");
      return 2;
    }

#  ifdef NEED_BOARDINIT
  boardctl(BOARDIOC_INIT, 0);
#  endif

  lv_init();
  lv_nuttx_dsc_init(&info);
#  ifdef CONFIG_LV_USE_NUTTX_LCD
  info.fb_path = "/dev/lcd0";
#  endif
  lv_nuttx_init(&info, &result);
  if (result.disp == NULL)
    {
      fprintf(stderr, "Living Canvas could not open /dev/lcd0\n");
      lv_nuttx_deinit(&result);
      lv_deinit();
      return 2;
    }

  screen = lv_screen_active();
  if (!create_background(result.disp, screen))
    {
      lv_nuttx_deinit(&result);
      lv_deinit();
      return 2;
    }

  if (!create_choice_overlay(result.disp, screen, false))
    {
      lv_nuttx_deinit(&result);
      lv_deinit();
      return 2;
    }
  create_competition_overlay(screen, qr_url, project_demo);

  for (;;)
    {
      uint32_t idle = lv_timer_handler();

      if (idle == 0u)
        {
          idle = 1u;
        }
      else if (idle > 50u)
        {
          idle = 50u;
        }

      usleep(idle * 1000u);
    }
}

int lc_display_run_competition_demo(const char *qr_url)
{
  return run_link_demo(qr_url, false);
}

int lc_display_run_project_demo(const char *project_url)
{
  return run_link_demo(project_url, true);
}

#else

int lc_display_run_preview(void)
{
  return 2;
}

int lc_display_run_image_preview(void)
{
  return 2;
}

int lc_display_run_greeting_preview(void)
{
  return 2;
}

int lc_display_run_choice_preview(void)
{
  return 2;
}

int lc_display_run_product(void)
{
  return 2;
}

int lc_display_run_product_accessory(const char *event_path,
                                     lc_presence_source_t source)
{
  (void)event_path;
  (void)source;
  return 2;
}

int lc_display_run_product_inputs(const char *presence_event_path,
                                  lc_presence_source_t source,
                                  const char *checkin_event_path)
{
  (void)presence_event_path;
  (void)source;
  (void)checkin_event_path;
  return 2;
}

int lc_display_run_competition_demo(const char *qr_url)
{
  (void)qr_url;
  return 2;
}

int lc_display_run_project_demo(const char *project_url)
{
  (void)project_url;
  return 2;
}

#endif
