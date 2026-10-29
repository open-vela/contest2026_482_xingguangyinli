#pragma once

#include <cstddef>
#include <cstdint>

constexpr std::size_t LIVING_CANVAS_LOCAL_DEVICE_ID_MAX = 6;
constexpr std::size_t LIVING_CANVAS_LOCAL_SESSION_MAX = 24;
constexpr std::size_t LIVING_CANVAS_LOCAL_PACKET_MAX = 160;
constexpr std::size_t LIVING_CANVAS_LOCAL_KEY_BYTES = 32;
constexpr std::size_t LIVING_CANVAS_LOCAL_TAG_BYTES = 32;
constexpr std::size_t LIVING_CANVAS_LOCAL_TAG_HEX_LENGTH = 64;

using LivingCanvasLocalHmac = bool (*)(
    void *context,
    const std::uint8_t key[LIVING_CANVAS_LOCAL_KEY_BYTES],
    const std::uint8_t *message,
    std::size_t messageLength,
    std::uint8_t output[LIVING_CANVAS_LOCAL_TAG_BYTES]);

enum class LivingCanvasLocalLinkState : std::uint8_t {
  Idle,
  Sending,
  Received,
  Retry,
};

class LivingCanvasLocalLink {
 public:
  LivingCanvasLocalLink(
      const char *deviceId,
      const char *sessionId,
      const std::uint8_t key[LIVING_CANVAS_LOCAL_KEY_BYTES],
      LivingCanvasLocalHmac hmac,
      void *hmacContext);

  bool ready() const;
  LivingCanvasLocalLinkState state() const;
  std::uint32_t sequence() const;
  std::uint8_t attempts() const;

  bool beginCheckin(std::uint32_t nowMs);
  bool retry(std::uint32_t nowMs);
  bool shouldSend(std::uint32_t nowMs) const;
  bool formatRequest(char *buffer, std::size_t bufferSize) const;
  void markSent(std::uint32_t nowMs);
  void update(std::uint32_t nowMs);
  bool acceptAck(const std::uint8_t *packet,
                 std::size_t packetLength,
                 std::uint32_t nowMs);

 private:
  char deviceId_[LIVING_CANVAS_LOCAL_DEVICE_ID_MAX];
  char sessionId_[LIVING_CANVAS_LOCAL_SESSION_MAX];
  std::uint8_t key_[LIVING_CANVAS_LOCAL_KEY_BYTES];
  LivingCanvasLocalHmac hmac_;
  void *hmacContext_;
  LivingCanvasLocalLinkState state_;
  std::uint32_t sequence_;
  std::uint32_t nextActionAtMs_;
  std::uint32_t stateStartedAtMs_;
  std::uint8_t attempts_;
  bool awaitingAck_;
  bool ready_;
};
