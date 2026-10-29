#ifndef LIVING_CANVAS_LC_DISPLAY_H
#define LIVING_CANVAS_LC_DISPLAY_H

#include "lc_presence.h"

int lc_display_run_preview(void);
int lc_display_run_image_preview(void);
int lc_display_run_greeting_preview(void);
int lc_display_run_choice_preview(void);
int lc_display_run_product(void);
int lc_display_run_product_accessory(const char *event_path,
                                     lc_presence_source_t source);
int lc_display_run_product_inputs(const char *presence_event_path,
                                  lc_presence_source_t source,
                                  const char *checkin_event_path);
int lc_display_run_competition_demo(const char *qr_url);
int lc_display_run_project_demo(const char *project_url);

#endif
