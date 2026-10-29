#include "living_canvas_local_link.h"

#include <cstdio>
#include <cstring>
#include <limits>

namespace {

constexpr std::uint32_t kAckTimeoutMs = 500;
constexpr std::uint8_t kMaxAttempts = 3;
constexpr std::size_t kAckFieldCount = 6;

bool due(std::uint32_t nowMs, std::uint32_t deadlineMs) {
  return static_cast<std::int32_t>(nowMs - deadlineMs) >= 0;
}

bool validDevice(const char *deviceId) {
  return deviceId != nullptr &&
         (std::strcmp(deviceId, "cat_a") == 0 ||
          std::strcmp(deviceId, "cat_b") == 0);
}

bool validSession(const char *sessionId) {
  if (sessionId == nullptr) return false;
  const std::size_t length = std::strlen(sessionId);
  if (length == 0 || length >= LIVING_CANVAS_LOCAL_SESSION_MAX) return false;
  for (std::size_t i = 0; i < length; ++i) {
    const char value = sessionId[i];
    const bool lower = value >= 'a' && value <= 'z';
    const bool upper = value >= 'A' && value <= 'Z';
    const bool digit = value >= '0' && value <= '9';
    if (!lower && !upper && !digit && value != '_' && value != '-') {
      return false;
    }
  }
  return true;
}

bool parseSequence(const char *field, std::uint32_t *sequence) {
  if (field == nullptr || sequence == nullptr || field[0] < '1' ||
      field[0] > '9') {
    return false;
  }
  std::uint64_t value = 0;
  for (std::size_t i = 0; field[i] != '\0'; ++i) {
    if (field[i] < '0' || field[i] > '9') return false;
    value = value * 10u + static_cast<unsigned>(field[i] - '0');
    if (value > std::numeric_limits<std::uint32_t>::max()) return false;
  }
  *sequence = static_cast<std::uint32_t>(value);
  return *sequence != 0;
}

int lowerHexValue(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
}

bool decodeTag(const char *hex,
               std::uint8_t output[LIVING_CANVAS_LOCAL_TAG_BYTES]) {
  if (hex == nullptr || output == nullptr ||
      std::strlen(hex) != LIVING_CANVAS_LOCAL_TAG_HEX_LENGTH) {
    return false;
  }
  for (std::size_t i = 0; i < LIVING_CANVAS_LOCAL_TAG_BYTES; ++i) {
    const int high = lowerHexValue(hex[i * 2]);
    const int low = lowerHexValue(hex[i * 2 + 1]);
    if (high < 0 || low < 0) return false;
    output[i] = static_cast<std::uint8_t>((high << 4) | low);
  }
  return true;
}

void encodeTag(const std::uint8_t tag[LIVING_CANVAS_LOCAL_TAG_BYTES],
               char output[LIVING_CANVAS_LOCAL_TAG_HEX_LENGTH + 1]) {
  constexpr char kHex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < LIVING_CANVAS_LOCAL_TAG_BYTES; ++i) {
    output[i * 2] = kHex[tag[i] >> 4];
    output[i * 2 + 1] = kHex[tag[i] & 0x0f];
  }
  output[LIVING_CANVAS_LOCAL_TAG_HEX_LENGTH] = '\0';
}

bool constantTimeEqual(const std::uint8_t *left, const std::uint8_t *right,
                       std::size_t length) {
  if (left == nullptr || right == nullptr) return false;
  std::uint8_t difference = 0;
  for (std::size_t i = 0; i < length; ++i) {
    difference |= static_cast<std::uint8_t>(left[i] ^ right[i]);
  }
  return difference == 0;
}

}  // namespace

LivingCanvasLocalLink::LivingCanvasLocalLink(
    const char *deviceId, const char *sessionId,
    const std::uint8_t key[LIVING_CANVAS_LOCAL_KEY_BYTES],
    LivingCanvasLocalHmac hmac, void *hmacContext)
    : deviceId_{},
      sessionId_{},
      key_{},
      hmac_(hmac),
      hmacContext_(hmacContext),
      state_(LivingCanvasLocalLinkState::Idle),
      sequence_(0),
      nextActionAtMs_(0),
      stateStartedAtMs_(0),
      attempts_(0),
      awaitingAck_(false),
      ready_(validDevice(deviceId) && validSession(sessionId) &&
             key != nullptr && hmac != nullptr) {
  if (!ready_) return;
  std::memcpy(deviceId_, deviceId, std::strlen(deviceId) + 1);
  std::memcpy(sessionId_, sessionId, std::strlen(sessionId) + 1);
  std::memcpy(key_, key, sizeof(key_));
}

bool LivingCanvasLocalLink::ready() const { return ready_; }

LivingCanvasLocalLinkState LivingCanvasLocalLink::state() const {
  return state_;
}

std::uint32_t LivingCanvasLocalLink::sequence() const { return sequence_; }

std::uint8_t LivingCanvasLocalLink::attempts() const { return attempts_; }

bool LivingCanvasLocalLink::beginCheckin(std::uint32_t nowMs) {
  if (!ready_ || (state_ != LivingCanvasLocalLinkState::Idle &&
                  state_ != LivingCanvasLocalLinkState::Received) ||
      sequence_ == std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  ++sequence_;
  attempts_ = 0;
  awaitingAck_ = false;
  nextActionAtMs_ = nowMs;
  stateStartedAtMs_ = nowMs;
  state_ = LivingCanvasLocalLinkState::Sending;
  return true;
}

bool LivingCanvasLocalLink::retry(std::uint32_t nowMs) {
  if (!ready_ || state_ != LivingCanvasLocalLinkState::Retry ||
      sequence_ == 0) {
    return false;
  }
  attempts_ = 0;
  awaitingAck_ = false;
  nextActionAtMs_ = nowMs;
  stateStartedAtMs_ = nowMs;
  state_ = LivingCanvasLocalLinkState::Sending;
  return true;
}

bool LivingCanvasLocalLink::shouldSend(std::uint32_t nowMs) const {
  return ready_ && state_ == LivingCanvasLocalLinkState::Sending &&
         !awaitingAck_ && attempts_ < kMaxAttempts &&
         due(nowMs, nextActionAtMs_);
}

bool LivingCanvasLocalLink::formatRequest(char *buffer,
                                          std::size_t bufferSize) const {
  char prefix[LIVING_CANVAS_LOCAL_PACKET_MAX];
  char tagHex[LIVING_CANVAS_LOCAL_TAG_HEX_LENGTH + 1];
  std::uint8_t tag[LIVING_CANVAS_LOCAL_TAG_BYTES];
  if (!ready_ || sequence_ == 0 || buffer == nullptr || bufferSize == 0) {
    return false;
  }
  const int prefixLength = std::snprintf(
      prefix, sizeof(prefix), "LC2,%s,%s,%lu,checkin,1", deviceId_, sessionId_,
      static_cast<unsigned long>(sequence_));
  if (prefixLength <= 0 ||
      static_cast<std::size_t>(prefixLength) >= sizeof(prefix) ||
      !hmac_(hmacContext_, key_,
             reinterpret_cast<const std::uint8_t *>(prefix),
             static_cast<std::size_t>(prefixLength), tag)) {
    buffer[0] = '\0';
    return false;
  }
  encodeTag(tag, tagHex);
  const int written =
      std::snprintf(buffer, bufferSize, "%s,%s", prefix, tagHex);
  if (written <= 0 || static_cast<std::size_t>(written) >= bufferSize) {
    buffer[0] = '\0';
    return false;
  }
  return true;
}

void LivingCanvasLocalLink::markSent(std::uint32_t nowMs) {
  if (!shouldSend(nowMs)) return;
  ++attempts_;
  awaitingAck_ = true;
  nextActionAtMs_ = nowMs + kAckTimeoutMs;
}

void LivingCanvasLocalLink::update(std::uint32_t nowMs) {
  if (state_ != LivingCanvasLocalLinkState::Sending || !awaitingAck_ ||
      !due(nowMs, nextActionAtMs_)) {
    return;
  }
  awaitingAck_ = false;
  if (attempts_ >= kMaxAttempts) {
    state_ = LivingCanvasLocalLinkState::Retry;
    stateStartedAtMs_ = nowMs;
    return;
  }
  nextActionAtMs_ = nowMs;
}

bool LivingCanvasLocalLink::acceptAck(const std::uint8_t *packet,
                                      std::size_t packetLength,
                                      std::uint32_t nowMs) {
  char copy[LIVING_CANVAS_LOCAL_PACKET_MAX];
  char *fields[kAckFieldCount];
  std::uint8_t receivedTag[LIVING_CANVAS_LOCAL_TAG_BYTES];
  std::uint8_t expectedTag[LIVING_CANVAS_LOCAL_TAG_BYTES];
  std::uint32_t ackSequence = 0;
  std::size_t prefixLength = 0;

  if (!ready_ || state_ != LivingCanvasLocalLinkState::Sending ||
      packet == nullptr || packetLength == 0 || packetLength >= sizeof(copy) ||
      std::memchr(packet, '\0', packetLength) != nullptr) {
    return false;
  }
  std::memcpy(copy, packet, packetLength);
  copy[packetLength] = '\0';
  char *cursor = copy;
  for (std::size_t i = 0; i < kAckFieldCount; ++i) {
    fields[i] = cursor;
    char *separator = std::strchr(cursor, ',');
    if (i + 1 < kAckFieldCount) {
      if (separator == nullptr) return false;
      if (i + 2 == kAckFieldCount) {
        prefixLength = static_cast<std::size_t>(separator - copy);
      }
      *separator = '\0';
      cursor = separator + 1;
    } else if (separator != nullptr) {
      return false;
    }
  }

  if (std::strcmp(fields[0], "LC2ACK") != 0 ||
      std::strcmp(fields[1], deviceId_) != 0 ||
      std::strcmp(fields[2], sessionId_) != 0 ||
      !parseSequence(fields[3], &ackSequence) || ackSequence != sequence_ ||
      (std::strcmp(fields[4], "accepted") != 0 &&
       std::strcmp(fields[4], "duplicate") != 0) ||
      !decodeTag(fields[5], receivedTag) || prefixLength == 0 ||
      !hmac_(hmacContext_, key_, packet, prefixLength, expectedTag) ||
      !constantTimeEqual(receivedTag, expectedTag, sizeof(receivedTag))) {
    return false;
  }

  awaitingAck_ = false;
  stateStartedAtMs_ = nowMs;
  state_ = LivingCanvasLocalLinkState::Received;
  return true;
}
