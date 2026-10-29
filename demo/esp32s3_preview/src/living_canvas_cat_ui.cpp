#include "living_canvas_cat_ui.h"

#include <cstring>

namespace {

constexpr std::uint32_t kReceivedDurationMs = 2000;
constexpr std::uint16_t kCatAAccent565 = 0xFBA6;
constexpr std::uint16_t kCatBAccent565 = 0x7A5F;
constexpr std::uint16_t kUnknownAccent565 = 0x8410;

LivingCanvasCatIdentity identityFor(const char *deviceId) {
  if (deviceId != nullptr && std::strcmp(deviceId, "cat_a") == 0) {
    return LivingCanvasCatIdentity::CatA;
  }
  if (deviceId != nullptr && std::strcmp(deviceId, "cat_b") == 0) {
    return LivingCanvasCatIdentity::CatB;
  }
  return LivingCanvasCatIdentity::Unknown;
}

}  // namespace

LivingCanvasCatUi::LivingCanvasCatUi(const char *deviceId)
    : identity_(identityFor(deviceId)),
      state_(LivingCanvasCatUiState::Idle),
      pendingSequence_(0),
      stateStartedAtMs_(0) {}

LivingCanvasCatIdentity LivingCanvasCatUi::identity() const { return identity_; }

LivingCanvasCatUiState LivingCanvasCatUi::state() const { return state_; }

const char *LivingCanvasCatUi::label() const {
  switch (identity_) {
    case LivingCanvasCatIdentity::CatA:
      return "CAT A";
    case LivingCanvasCatIdentity::CatB:
      return "CAT B";
    case LivingCanvasCatIdentity::Unknown:
      return "CAT PREVIEW";
  }
  return "CAT PREVIEW";
}

std::uint16_t LivingCanvasCatUi::accent565() const {
  switch (identity_) {
    case LivingCanvasCatIdentity::CatA:
      return kCatAAccent565;
    case LivingCanvasCatIdentity::CatB:
      return kCatBAccent565;
    case LivingCanvasCatIdentity::Unknown:
      return kUnknownAccent565;
  }
  return kUnknownAccent565;
}

std::uint32_t LivingCanvasCatUi::pendingSequence() const {
  return pendingSequence_;
}

bool LivingCanvasCatUi::onExplicitTouch(std::uint32_t sequence,
                                        std::uint32_t nowMs) {
  if (sequence == 0 || state_ == LivingCanvasCatUiState::Sending ||
      state_ == LivingCanvasCatUiState::Received) {
    return false;
  }
  if (state_ == LivingCanvasCatUiState::Retry && sequence != pendingSequence_) {
    return false;
  }
  pendingSequence_ = sequence;
  stateStartedAtMs_ = nowMs;
  state_ = LivingCanvasCatUiState::Sending;
  return true;
}

bool LivingCanvasCatUi::onMatchingAck(std::uint32_t sequence,
                                      std::uint32_t nowMs) {
  if (state_ != LivingCanvasCatUiState::Sending || sequence == 0 ||
      sequence != pendingSequence_) {
    return false;
  }
  state_ = LivingCanvasCatUiState::Received;
  stateStartedAtMs_ = nowMs;
  return true;
}

bool LivingCanvasCatUi::onDeliveryFailed(std::uint32_t sequence,
                                         std::uint32_t nowMs) {
  if (state_ != LivingCanvasCatUiState::Sending || sequence == 0 ||
      sequence != pendingSequence_) {
    return false;
  }
  state_ = LivingCanvasCatUiState::Retry;
  stateStartedAtMs_ = nowMs;
  return true;
}

void LivingCanvasCatUi::update(std::uint32_t nowMs) {
  if (state_ == LivingCanvasCatUiState::Received &&
      nowMs - stateStartedAtMs_ >= kReceivedDurationMs) {
    state_ = LivingCanvasCatUiState::Idle;
    pendingSequence_ = 0;
    stateStartedAtMs_ = nowMs;
  }
}
