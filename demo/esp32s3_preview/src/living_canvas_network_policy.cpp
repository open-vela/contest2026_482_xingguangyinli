#include "living_canvas_network_policy.h"

#include <cstdint>

LivingCanvasNetworkPolicy::LivingCanvasNetworkPolicy(
    uint32_t associationWindowMs)
    : associationWindowMs_(associationWindowMs),
      deadlineMs_(0),
      activeSlot_(0),
      begun_(false),
      connected_(false) {}

void LivingCanvasNetworkPolicy::begin(uint32_t nowMs) {
  activeSlot_ = 0;
  connected_ = false;
  begun_ = true;
  deadlineMs_ = nowMs + associationWindowMs_;
}

void LivingCanvasNetworkPolicy::observeConnected() { connected_ = true; }

void LivingCanvasNetworkPolicy::observeDisconnected(uint32_t nowMs) {
  connected_ = false;
  deadlineMs_ = nowMs + associationWindowMs_;
}

bool LivingCanvasNetworkPolicy::shouldRotate(uint32_t nowMs) const {
  return begun_ && !connected_ &&
         static_cast<int32_t>(nowMs - deadlineMs_) >= 0;
}

void LivingCanvasNetworkPolicy::rotate(uint32_t nowMs) {
  activeSlot_ = static_cast<uint8_t>(activeSlot_ == 0 ? 1 : 0);
  connected_ = false;
  begun_ = true;
  deadlineMs_ = nowMs + associationWindowMs_;
}

uint8_t LivingCanvasNetworkPolicy::activeSlot() const { return activeSlot_; }

bool LivingCanvasNetworkPolicy::connected() const { return connected_; }

const char *LivingCanvasNetworkPolicy::select(
    const char *primary, const char *fallback) const {
  return activeSlot_ == 0 ? primary : fallback;
}
