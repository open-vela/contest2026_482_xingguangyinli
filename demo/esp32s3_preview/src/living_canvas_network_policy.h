#pragma once

#include <cstdint>

class LivingCanvasNetworkPolicy {
 public:
  explicit LivingCanvasNetworkPolicy(uint32_t associationWindowMs);

  void begin(uint32_t nowMs);
  void observeConnected();
  void observeDisconnected(uint32_t nowMs);
  bool shouldRotate(uint32_t nowMs) const;
  void rotate(uint32_t nowMs);

  uint8_t activeSlot() const;
  bool connected() const;
  const char *select(const char *primary, const char *fallback) const;

 private:
  uint32_t associationWindowMs_;
  uint32_t deadlineMs_;
  uint8_t activeSlot_;
  bool begun_;
  bool connected_;
};
