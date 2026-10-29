#include <cassert>
#include <cstring>

#include "living_canvas_render_policy.h"

static void missing_private_config_is_an_honest_setup_state() {
  const LivingCanvasCatRenderPlan plan = livingCanvasCatRenderPlan(
      LivingCanvasCatUiState::Idle, false, false, 0);
  assert(plan.expression == LivingCanvasCatExpression::Concerned);
  assert(std::strcmp(plan.headline, "SETUP REQUIRED") == 0);
  assert(!plan.showAction);
  assert(!plan.networkReady);
}

static void configured_offline_device_shows_connecting_not_success() {
  const LivingCanvasCatRenderPlan plan = livingCanvasCatRenderPlan(
      LivingCanvasCatUiState::Idle, true, false, 360);
  assert(plan.expression == LivingCanvasCatExpression::Working);
  assert(std::strcmp(plan.headline, "CONNECTING TO FRAME") == 0);
  assert(plan.pulseStep == 2);
  assert(!plan.showAction);
  assert(!plan.networkReady);
}

static void connected_idle_device_blinks_and_accepts_one_touch() {
  LivingCanvasCatRenderPlan plan = livingCanvasCatRenderPlan(
      LivingCanvasCatUiState::Idle, true, true, 0);
  assert(plan.expression == LivingCanvasCatExpression::Blink);
  assert(std::strcmp(plan.headline, "TAP TO CHECK IN") == 0);
  assert(plan.showAction);
  assert(plan.networkReady);

  plan = livingCanvasCatRenderPlan(
      LivingCanvasCatUiState::Idle, true, true, 300);
  assert(plan.expression == LivingCanvasCatExpression::Awake);
}

static void delivery_states_never_claim_success_without_an_ack() {
  LivingCanvasCatRenderPlan plan = livingCanvasCatRenderPlan(
      LivingCanvasCatUiState::Sending, true, true, 540);
  assert(plan.expression == LivingCanvasCatExpression::Working);
  assert(std::strcmp(plan.headline, "TELLING THE FRAME") == 0);
  assert(!plan.showAction);

  plan = livingCanvasCatRenderPlan(
      LivingCanvasCatUiState::Retry, true, true, 600);
  assert(plan.expression == LivingCanvasCatExpression::Concerned);
  assert(std::strcmp(plan.headline, "NO RESPONSE") == 0);
  assert(plan.showAction);

  plan = livingCanvasCatRenderPlan(
      LivingCanvasCatUiState::Received, true, true, 700);
  assert(plan.expression == LivingCanvasCatExpression::Happy);
  assert(std::strcmp(plan.headline, "FRAME RECEIVED IT") == 0);
  assert(!plan.showAction);
}

int main() {
  missing_private_config_is_an_honest_setup_state();
  configured_offline_device_shows_connecting_not_success();
  connected_idle_device_blinks_and_accepts_one_touch();
  delivery_states_never_claim_success_without_an_ack();
  return 0;
}
