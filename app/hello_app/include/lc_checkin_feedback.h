#ifndef LIVING_CANVAS_LC_CHECKIN_FEEDBACK_H
#define LIVING_CANVAS_LC_CHECKIN_FEEDBACK_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  LC_CHECKIN_FEEDBACK_NONE = 0,
  LC_CHECKIN_FEEDBACK_CAT_A,
  LC_CHECKIN_FEEDBACK_CAT_B
} lc_checkin_feedback_identity_t;

typedef struct
{
  lc_checkin_feedback_identity_t identity;
  lc_checkin_feedback_identity_t pending_chime;
  uint32_t duration_ms;
  uint32_t expires_at_ms;
  bool active;
  bool initialized;
} lc_checkin_feedback_t;

void lc_checkin_feedback_init(lc_checkin_feedback_t *feedback,
                              uint32_t duration_ms);
bool lc_checkin_feedback_on_accepted(lc_checkin_feedback_t *feedback,
                                     const char *device_id,
                                     uint32_t now_ms);
void lc_checkin_feedback_update(lc_checkin_feedback_t *feedback,
                                uint32_t now_ms);
bool lc_checkin_feedback_active(const lc_checkin_feedback_t *feedback);
const char *lc_checkin_feedback_message(
  const lc_checkin_feedback_t *feedback);
lc_checkin_feedback_identity_t lc_checkin_feedback_take_chime(
  lc_checkin_feedback_t *feedback);

#endif
