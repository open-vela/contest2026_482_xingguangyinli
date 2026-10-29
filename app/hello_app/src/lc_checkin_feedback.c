#include "lc_checkin_feedback.h"

#include <stddef.h>
#include <string.h>

static bool due(uint32_t now_ms, uint32_t deadline_ms)
{
  return (int32_t)(now_ms - deadline_ms) >= 0;
}

void lc_checkin_feedback_init(lc_checkin_feedback_t *feedback,
                              uint32_t duration_ms)
{
  if (feedback == NULL)
    {
      return;
    }
  memset(feedback, 0, sizeof(*feedback));
  feedback->duration_ms = duration_ms;
  feedback->initialized = duration_ms > 0u && duration_ms <= INT32_MAX;
}

bool lc_checkin_feedback_on_accepted(lc_checkin_feedback_t *feedback,
                                     const char *device_id,
                                     uint32_t now_ms)
{
  lc_checkin_feedback_identity_t identity;

  if (feedback == NULL || !feedback->initialized || device_id == NULL)
    {
      return false;
    }
  if (strcmp(device_id, "cat_a") == 0)
    {
      identity = LC_CHECKIN_FEEDBACK_CAT_A;
    }
  else if (strcmp(device_id, "cat_b") == 0)
    {
      identity = LC_CHECKIN_FEEDBACK_CAT_B;
    }
  else
    {
      return false;
    }

  feedback->identity = identity;
  feedback->pending_chime = identity;
  feedback->expires_at_ms = now_ms + feedback->duration_ms;
  feedback->active = true;
  return true;
}

void lc_checkin_feedback_update(lc_checkin_feedback_t *feedback,
                                uint32_t now_ms)
{
  if (feedback == NULL || !feedback->initialized || !feedback->active ||
      !due(now_ms, feedback->expires_at_ms))
    {
      return;
    }
  feedback->active = false;
  feedback->identity = LC_CHECKIN_FEEDBACK_NONE;
}

bool lc_checkin_feedback_active(const lc_checkin_feedback_t *feedback)
{
  return feedback != NULL && feedback->initialized && feedback->active;
}

const char *lc_checkin_feedback_message(
  const lc_checkin_feedback_t *feedback)
{
  if (!lc_checkin_feedback_active(feedback))
    {
      return "";
    }
  if (feedback->identity == LC_CHECKIN_FEEDBACK_CAT_A)
    {
      return "猫A已向画框报到";
    }
  if (feedback->identity == LC_CHECKIN_FEEDBACK_CAT_B)
    {
      return "猫B已向画框报到";
    }
  return "";
}

lc_checkin_feedback_identity_t lc_checkin_feedback_take_chime(
  lc_checkin_feedback_t *feedback)
{
  lc_checkin_feedback_identity_t pending;

  if (feedback == NULL || !feedback->initialized)
    {
      return LC_CHECKIN_FEEDBACK_NONE;
    }
  pending = feedback->pending_chime;
  feedback->pending_chime = LC_CHECKIN_FEEDBACK_NONE;
  return pending;
}
