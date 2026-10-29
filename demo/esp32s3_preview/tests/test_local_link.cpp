#include "living_canvas_local_link.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

constexpr std::uint8_t kTestKey[LIVING_CANVAS_LOCAL_KEY_BYTES] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

bool testHmac(void *, const std::uint8_t key[LIVING_CANVAS_LOCAL_KEY_BYTES],
              const std::uint8_t *message, std::size_t messageLength,
              std::uint8_t output[LIVING_CANVAS_LOCAL_TAG_BYTES]) {
  std::uint32_t state = 2166136261u;
  for (std::size_t i = 0; i < LIVING_CANVAS_LOCAL_KEY_BYTES; ++i) {
    state = (state ^ key[i]) * 16777619u;
  }
  for (std::size_t i = 0; i < messageLength; ++i) {
    state = (state ^ message[i]) * 16777619u;
  }
  for (std::size_t i = 0; i < LIVING_CANVAS_LOCAL_TAG_BYTES; ++i) {
    state ^= state >> 13;
    state *= 0x5bd1e995u;
    output[i] = static_cast<std::uint8_t>(state >> ((i % 4u) * 8u));
  }
  return true;
}

void encodeHex(const std::uint8_t tag[LIVING_CANVAS_LOCAL_TAG_BYTES],
               char output[LIVING_CANVAS_LOCAL_TAG_HEX_LENGTH + 1]) {
  constexpr char kHex[] = "0123456789abcdef";
  for (std::size_t i = 0; i < LIVING_CANVAS_LOCAL_TAG_BYTES; ++i) {
    output[i * 2] = kHex[tag[i] >> 4];
    output[i * 2 + 1] = kHex[tag[i] & 0x0f];
  }
  output[LIVING_CANVAS_LOCAL_TAG_HEX_LENGTH] = '\0';
}

void makeAck(const char *device, const char *session, std::uint32_t sequence,
             const char *status, char *output, std::size_t outputSize) {
  char prefix[96];
  std::uint8_t tag[LIVING_CANVAS_LOCAL_TAG_BYTES];
  char hex[LIVING_CANVAS_LOCAL_TAG_HEX_LENGTH + 1];
  const int written = std::snprintf(prefix, sizeof(prefix),
                                    "LC2ACK,%s,%s,%lu,%s", device, session,
                                    static_cast<unsigned long>(sequence), status);
  assert(written > 0 && static_cast<std::size_t>(written) < sizeof(prefix));
  assert(testHmac(nullptr, kTestKey,
                  reinterpret_cast<const std::uint8_t *>(prefix),
                  std::strlen(prefix), tag));
  encodeHex(tag, hex);
  assert(std::snprintf(output, outputSize, "%s,%s", prefix, hex) > 0);
}

void requestIsStableAcrossFiniteRetries() {
  LivingCanvasLocalLink link("cat_a", "boot_A-1", kTestKey, testHmac,
                             nullptr);
  char first[LIVING_CANVAS_LOCAL_PACKET_MAX];
  char retried[LIVING_CANVAS_LOCAL_PACKET_MAX];

  assert(link.ready());
  assert(link.state() == LivingCanvasLocalLinkState::Idle);
  assert(link.beginCheckin(100));
  assert(link.sequence() == 1);
  assert(link.shouldSend(100));
  assert(link.formatRequest(first, sizeof(first)));

  link.markSent(100);
  assert(link.attempts() == 1);
  assert(!link.shouldSend(599));
  link.update(600);
  assert(link.shouldSend(600));
  assert(link.formatRequest(retried, sizeof(retried)));
  assert(std::strcmp(first, retried) == 0);

  link.markSent(600);
  link.update(1100);
  assert(link.shouldSend(1100));
  link.markSent(1100);
  link.update(1599);
  assert(link.state() == LivingCanvasLocalLinkState::Sending);
  link.update(1600);
  assert(link.state() == LivingCanvasLocalLinkState::Retry);
  assert(link.sequence() == 1);
  assert(link.retry(2000));
  assert(link.sequence() == 1);
  assert(link.shouldSend(2000));
  assert(link.formatRequest(retried, sizeof(retried)));
  assert(std::strcmp(first, retried) == 0);
}

void strictAckMatchIsRequiredForSuccess() {
  LivingCanvasLocalLink link("cat_a", "boot_A-2", kTestKey, testHmac,
                             nullptr);
  char ack[LIVING_CANVAS_LOCAL_PACKET_MAX];

  assert(link.beginCheckin(10));
  link.markSent(10);
  makeAck("cat_b", "boot_A-2", 1, "accepted", ack, sizeof(ack));
  assert(!link.acceptAck(reinterpret_cast<const std::uint8_t *>(ack),
                         std::strlen(ack), 20));
  makeAck("cat_a", "wrong", 1, "accepted", ack, sizeof(ack));
  assert(!link.acceptAck(reinterpret_cast<const std::uint8_t *>(ack),
                         std::strlen(ack), 20));
  makeAck("cat_a", "boot_A-2", 2, "accepted", ack, sizeof(ack));
  assert(!link.acceptAck(reinterpret_cast<const std::uint8_t *>(ack),
                         std::strlen(ack), 20));
  makeAck("cat_a", "boot_A-2", 1, "rejected", ack, sizeof(ack));
  assert(!link.acceptAck(reinterpret_cast<const std::uint8_t *>(ack),
                         std::strlen(ack), 20));
  makeAck("cat_a", "boot_A-2", 1, "accepted", ack, sizeof(ack));
  ack[std::strlen(ack) - 1] = ack[std::strlen(ack) - 1] == '0' ? '1' : '0';
  assert(!link.acceptAck(reinterpret_cast<const std::uint8_t *>(ack),
                         std::strlen(ack), 20));
  assert(link.state() == LivingCanvasLocalLinkState::Sending);

  makeAck("cat_a", "boot_A-2", 1, "accepted", ack, sizeof(ack));
  assert(link.acceptAck(reinterpret_cast<const std::uint8_t *>(ack),
                        std::strlen(ack), 30));
  assert(link.state() == LivingCanvasLocalLinkState::Received);
}

void duplicateAckAlsoClosesALostResponse() {
  LivingCanvasLocalLink link("cat_b", "boot_B-7", kTestKey, testHmac,
                             nullptr);
  char ack[LIVING_CANVAS_LOCAL_PACKET_MAX];

  assert(link.beginCheckin(10));
  link.markSent(10);
  makeAck("cat_b", "boot_B-7", 1, "duplicate", ack, sizeof(ack));
  assert(link.acceptAck(reinterpret_cast<const std::uint8_t *>(ack),
                        std::strlen(ack), 20));
  assert(link.state() == LivingCanvasLocalLinkState::Received);
}

void invalidIdentityAndSessionFailClosed() {
  LivingCanvasLocalLink unknown("cat_c", "boot_C", kTestKey, testHmac,
                                nullptr);
  LivingCanvasLocalLink badSession("cat_a", "bad session", kTestKey,
                                   testHmac, nullptr);
  LivingCanvasLocalLink nullKey("cat_a", "boot_A", nullptr, testHmac,
                                nullptr);
  assert(!unknown.ready());
  assert(!badSession.ready());
  assert(!nullKey.ready());
  assert(!unknown.beginCheckin(1));
}

}  // namespace

int main() {
  requestIsStableAcrossFiniteRetries();
  strictAckMatchIsRequiredForSuccess();
  duplicateAckAlsoClosesALostResponse();
  invalidIdentityAndSessionFailClosed();
  return 0;
}
