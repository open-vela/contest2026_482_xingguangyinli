#include "living_canvas_cat_ui.h"

#include <cassert>
#include <cstring>

static void identity_is_stable_and_distinct() {
  LivingCanvasCatUi catA("cat_a");
  LivingCanvasCatUi catB("cat_b");
  LivingCanvasCatUi unknown("other");

  assert(catA.identity() == LivingCanvasCatIdentity::CatA);
  assert(catB.identity() == LivingCanvasCatIdentity::CatB);
  assert(unknown.identity() == LivingCanvasCatIdentity::Unknown);
  assert(std::strcmp(catA.label(), catB.label()) != 0);
  assert(catA.accent565() != catB.accent565());
}

static void matching_ack_is_required_for_success() {
  LivingCanvasCatUi ui("cat_a");

  assert(ui.state() == LivingCanvasCatUiState::Idle);
  assert(ui.onExplicitTouch(7, 1000));
  assert(ui.state() == LivingCanvasCatUiState::Sending);
  assert(ui.pendingSequence() == 7);
  assert(!ui.onExplicitTouch(8, 1010));
  assert(!ui.onMatchingAck(8, 1100));
  assert(ui.state() == LivingCanvasCatUiState::Sending);
  assert(ui.onMatchingAck(7, 1100));
  assert(ui.state() == LivingCanvasCatUiState::Received);

  ui.update(3099);
  assert(ui.state() == LivingCanvasCatUiState::Received);
  ui.update(3100);
  assert(ui.state() == LivingCanvasCatUiState::Idle);
}

static void failure_keeps_a_retryable_sequence() {
  LivingCanvasCatUi ui("cat_b");

  assert(ui.onExplicitTouch(11, 400));
  assert(ui.onDeliveryFailed(11, 900));
  assert(ui.state() == LivingCanvasCatUiState::Retry);
  assert(ui.pendingSequence() == 11);
  assert(!ui.onDeliveryFailed(12, 901));
  assert(ui.onExplicitTouch(11, 1000));
  assert(ui.state() == LivingCanvasCatUiState::Sending);
  assert(ui.onMatchingAck(11, 1080));
  assert(ui.state() == LivingCanvasCatUiState::Received);
}

int main() {
  identity_is_stable_and_distinct();
  matching_ack_is_required_for_success();
  failure_keeps_a_retryable_sequence();
  return 0;
}
