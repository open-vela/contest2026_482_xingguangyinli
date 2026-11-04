#include "living_canvas_render_policy.h"

namespace {

LivingCanvasCatRenderPlan plan(
    LivingCanvasCatExpression expression, const char *headline,
    const char *detail, std::uint8_t pulseStep, bool showAction,
    bool networkReady) {
  return {expression, headline, detail, pulseStep, showAction, networkReady};
}

}  // namespace

LivingCanvasCatRenderPlan livingCanvasCatRenderPlan(
    LivingCanvasCatUiState state, bool configured, bool networkReady,
    std::uint32_t nowMs) {
  (void)nowMs;
  if (!configured) {
    return plan(LivingCanvasCatExpression::Concerned, "SETUP REQUIRED",
                "PRIVATE LINK NOT CONFIGURED", 0, false, false);
  }

  if (state == LivingCanvasCatUiState::Received) {
    return plan(LivingCanvasCatExpression::Happy, "FRAME SAYS HI BACK",
                "SIGNED REPLY VERIFIED", 0, false, networkReady);
  }
  if (!networkReady) {
    return plan(
        LivingCanvasCatExpression::Working, "CONNECTING TO FRAME",
        "WAITING FOR LOCAL NETWORK", 0, false, false);
  }
  if (state == LivingCanvasCatUiState::Sending) {
    return plan(
        LivingCanvasCatExpression::Working, "SAYING HI TO FRAME",
        "SIGNED SOCIAL EVENT", 0, false, true);
  }
  if (state == LivingCanvasCatUiState::Retry) {
    return plan(LivingCanvasCatExpression::Concerned, "NO RESPONSE",
                "TAP TO RETRY THE SAME EVENT", 0, true, true);
  }

  return plan(LivingCanvasCatExpression::Awake, "TAP TO SAY HI",
              "ONE CAT, ONE FAMILY MEMBER", 0, true, true);
}
