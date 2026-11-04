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
  const LivingCanvasCatRenderPlan first = livingCanvasCatRenderPlan(
      LivingCanvasCatUiState::Idle, true, false, 0);
  const LivingCanvasCatRenderPlan plan = livingCanvasCatRenderPlan(
      LivingCanvasCatUiState::Idle, true, false, 360);
  assert(plan.expression == LivingCanvasCatExpression::Working);
  assert(std::strcmp(plan.headline, "CONNECTING TO FRAME") == 0);
  assert(first.pulseStep == 0);
  assert(plan.pulseStep == first.pulseStep);
  assert(!plan.showAction);
  assert(!plan.networkReady);
}

static void disconnected_retry_waits_for_network_without_action() {
  const LivingCanvasCatRenderPlan plan = livingCanvasCatRenderPlan(
      LivingCanvasCatUiState::Retry, true, false, 900);
  assert(plan.expression == LivingCanvasCatExpression::Working);
  assert(std::strcmp(plan.headline, "CONNECTING TO FRAME") == 0);
  assert(std::strcmp(plan.detail, "WAITING FOR LOCAL NETWORK") == 0);
  assert(!plan.showAction);
  assert(!plan.networkReady);
}

static void disconnected_sending_waits_for_network_without_action() {
  const LivingCanvasCatRenderPlan plan = livingCanvasCatRenderPlan(
      LivingCanvasCatUiState::Sending, true, false, 1000);
  assert(plan.expression == LivingCanvasCatExpression::Working);
  assert(std::strcmp(plan.headline, "CONNECTING TO FRAME") == 0);
  assert(std::strcmp(plan.detail, "WAITING FOR LOCAL NETWORK") == 0);
  assert(!plan.showAction);
  assert(!plan.networkReady);
}

static void connected_idle_device_stays_stable_and_accepts_one_touch() {
  LivingCanvasCatRenderPlan plan = livingCanvasCatRenderPlan(
      LivingCanvasCatUiState::Idle, true, true, 0);
  assert(plan.expression == LivingCanvasCatExpression::Awake);
  assert(std::strcmp(plan.headline, "TAP TO SAY HI") == 0);
  assert(std::strcmp(plan.detail, "ONE CAT, ONE FAMILY MEMBER") == 0);
  assert(plan.showAction);
  assert(plan.networkReady);

  plan = livingCanvasCatRenderPlan(
      LivingCanvasCatUiState::Idle, true, true, 300);
  assert(plan.expression == LivingCanvasCatExpression::Awake);
  assert(plan.pulseStep == 0);
}

static void delivery_states_never_claim_success_without_an_ack() {
  LivingCanvasCatRenderPlan plan = livingCanvasCatRenderPlan(
      LivingCanvasCatUiState::Sending, true, true, 540);
  assert(plan.expression == LivingCanvasCatExpression::Working);
  assert(std::strcmp(plan.headline, "SAYING HI TO FRAME") == 0);
  assert(std::strcmp(plan.detail, "SIGNED SOCIAL EVENT") == 0);
  assert(plan.pulseStep == 0);
  assert(!plan.showAction);

  plan = livingCanvasCatRenderPlan(
      LivingCanvasCatUiState::Retry, true, true, 600);
  assert(plan.expression == LivingCanvasCatExpression::Concerned);
  assert(std::strcmp(plan.headline, "NO RESPONSE") == 0);
  assert(plan.showAction);

  plan = livingCanvasCatRenderPlan(
      LivingCanvasCatUiState::Received, true, true, 700);
  assert(plan.expression == LivingCanvasCatExpression::Happy);
  assert(std::strcmp(plan.headline, "FRAME SAYS HI BACK") == 0);
  assert(std::strcmp(plan.detail, "SIGNED REPLY VERIFIED") == 0);
  assert(!plan.showAction);
}

int main() {
  missing_private_config_is_an_honest_setup_state();
  configured_offline_device_shows_connecting_not_success();
  disconnected_retry_waits_for_network_without_action();
  disconnected_sending_waits_for_network_without_action();
  connected_idle_device_stays_stable_and_accepts_one_touch();
  delivery_states_never_claim_success_without_an_ack();
  return 0;
}
