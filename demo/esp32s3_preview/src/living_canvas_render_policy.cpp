#include "living_canvas_render_policy.h"

namespace {

constexpr std::uint32_t kPulseIntervalMs = 180;
constexpr std::uint32_t kBlinkCycleMs = 4200;
constexpr std::uint32_t kBlinkDurationMs = 180;

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
  if (!configured) {
    return plan(LivingCanvasCatExpression::Concerned, "SETUP REQUIRED",
                "PRIVATE LINK NOT CONFIGURED", 0, false, false);
  }

  if (state == LivingCanvasCatUiState::Sending) {
    return plan(
        LivingCanvasCatExpression::Working, "TELLING THE FRAME",
        "SIGNED EVENT IN FLIGHT",
        static_cast<std::uint8_t>((nowMs / kPulseIntervalMs) % 3u), false,
        networkReady);
  }
  if (state == LivingCanvasCatUiState::Received) {
    return plan(LivingCanvasCatExpression::Happy, "FRAME RECEIVED IT",
                "SIGNED ACK VERIFIED", 0, false, networkReady);
  }
  if (state == LivingCanvasCatUiState::Retry) {
    return plan(LivingCanvasCatExpression::Concerned, "NO RESPONSE",
                "TAP TO RETRY THE SAME EVENT", 0, true, networkReady);
  }

  if (!networkReady) {
    return plan(
        LivingCanvasCatExpression::Working, "CONNECTING TO FRAME",
        "WAITING FOR LOCAL NETWORK",
        static_cast<std::uint8_t>((nowMs / kPulseIntervalMs) % 3u), false,
        false);
  }

  const bool blinking = nowMs % kBlinkCycleMs < kBlinkDurationMs;
  return plan(
      blinking ? LivingCanvasCatExpression::Blink
               : LivingCanvasCatExpression::Awake,
      "TAP TO CHECK IN", "ONE TOUCH, ONE REAL EVENT", 0, true, true);
}
