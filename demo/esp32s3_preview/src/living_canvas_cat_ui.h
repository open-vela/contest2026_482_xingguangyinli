#pragma once

#include <cstdint>

enum class LivingCanvasCatIdentity : std::uint8_t {
  Unknown,
  CatA,
  CatB,
};

enum class LivingCanvasCatUiState : std::uint8_t {
  Idle,
  Sending,
  Received,
  Retry,
};

class LivingCanvasCatUi {
 public:
  explicit LivingCanvasCatUi(const char *deviceId);

  LivingCanvasCatIdentity identity() const;
  LivingCanvasCatUiState state() const;
  const char *label() const;
  std::uint16_t accent565() const;
  std::uint32_t pendingSequence() const;

  bool onExplicitTouch(std::uint32_t sequence, std::uint32_t nowMs);
  bool onMatchingAck(std::uint32_t sequence, std::uint32_t nowMs);
  bool onDeliveryFailed(std::uint32_t sequence, std::uint32_t nowMs);
  void update(std::uint32_t nowMs);

 private:
  LivingCanvasCatIdentity identity_;
  LivingCanvasCatUiState state_;
  std::uint32_t pendingSequence_;
  std::uint32_t stateStartedAtMs_;
};
