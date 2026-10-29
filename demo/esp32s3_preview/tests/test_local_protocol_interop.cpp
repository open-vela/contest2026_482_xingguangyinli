extern "C" {
#include "lc_local_accessory.h"
}

#include "living_canvas_local_link.h"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace {

constexpr std::uint8_t kCatAKey[LIVING_CANVAS_LOCAL_KEY_BYTES] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

constexpr std::uint8_t kCatBKey[LIVING_CANVAS_LOCAL_KEY_BYTES] = {
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f};

bool sharedTestHmac(
    void *, const std::uint8_t key[LIVING_CANVAS_LOCAL_KEY_BYTES],
    const std::uint8_t *message, std::size_t messageLength,
    std::uint8_t output[LIVING_CANVAS_LOCAL_TAG_BYTES]) {
  std::uint32_t state = 2166136261u;
  for (std::size_t index = 0; index < LIVING_CANVAS_LOCAL_KEY_BYTES;
       ++index) {
    state = (state ^ key[index]) * 16777619u;
  }
  for (std::size_t index = 0; index < messageLength; ++index) {
    state = (state ^ message[index]) * 16777619u;
  }
  for (std::size_t index = 0; index < LIVING_CANVAS_LOCAL_TAG_BYTES;
       ++index) {
    state ^= state >> 13;
    state *= 0x5bd1e995u;
    output[index] =
        static_cast<std::uint8_t>(state >> ((index % 4u) * 8u));
  }
  return true;
}

void acceptedRoundTripUsesTheSameWireContract() {
  lc_local_accessory_guard_t guard{};
  lc_local_accessory_event_t event{};
  LivingCanvasLocalLink link("cat_a", "boot_A-interop", kCatAKey,
                             sharedTestHmac, nullptr);
  char request[LIVING_CANVAS_LOCAL_PACKET_MAX]{};
  char ack[LIVING_CANVAS_LOCAL_PACKET_MAX]{};

  assert(lc_local_accessory_guard_init(&guard, kCatAKey, kCatBKey,
                                       sharedTestHmac, nullptr));
  assert(link.beginCheckin(100));
  assert(link.formatRequest(request, sizeof(request)));
  link.markSent(100);

  const lc_local_accessory_status_t status = lc_local_accessory_verify(
      &guard, reinterpret_cast<const std::uint8_t *>(request),
      std::strlen(request), &event);
  assert(status == LC_LOCAL_ACCESSORY_ACCEPTED);
  assert(std::strcmp(event.device_id, "cat_a") == 0);
  assert(std::strcmp(event.session_id, "boot_A-interop") == 0);
  assert(event.sequence == link.sequence());
  assert(lc_local_accessory_ack_format(&guard, &event, status, ack,
                                       sizeof(ack)));
  assert(link.acceptAck(reinterpret_cast<const std::uint8_t *>(ack),
                        std::strlen(ack), 120));
  assert(link.state() == LivingCanvasLocalLinkState::Received);
}

void aLostAckRetransmissionClosesWithDuplicateStatus() {
  lc_local_accessory_guard_t guard{};
  lc_local_accessory_event_t event{};
  LivingCanvasLocalLink link("cat_b", "boot_B-interop", kCatBKey,
                             sharedTestHmac, nullptr);
  char request[LIVING_CANVAS_LOCAL_PACKET_MAX]{};
  char ack[LIVING_CANVAS_LOCAL_PACKET_MAX]{};

  assert(lc_local_accessory_guard_init(&guard, kCatAKey, kCatBKey,
                                       sharedTestHmac, nullptr));
  assert(link.beginCheckin(200));
  assert(link.formatRequest(request, sizeof(request)));
  link.markSent(200);
  assert(lc_local_accessory_verify(
             &guard, reinterpret_cast<const std::uint8_t *>(request),
             std::strlen(request), &event) == LC_LOCAL_ACCESSORY_ACCEPTED);

  link.update(700);
  assert(link.shouldSend(700));
  link.markSent(700);
  assert(lc_local_accessory_verify(
             &guard, reinterpret_cast<const std::uint8_t *>(request),
             std::strlen(request), &event) == LC_LOCAL_ACCESSORY_DUPLICATE);
  assert(lc_local_accessory_ack_format(
      &guard, &event, LC_LOCAL_ACCESSORY_DUPLICATE, ack, sizeof(ack)));
  assert(link.acceptAck(reinterpret_cast<const std::uint8_t *>(ack),
                        std::strlen(ack), 710));
  assert(link.state() == LivingCanvasLocalLinkState::Received);
}

void deviceKeysCannotCrossAuthenticate() {
  lc_local_accessory_guard_t guard{};
  lc_local_accessory_event_t event{};
  LivingCanvasLocalLink wronglyKeyedCatB(
      "cat_b", "boot_B-wrong-key", kCatAKey, sharedTestHmac, nullptr);
  char request[LIVING_CANVAS_LOCAL_PACKET_MAX]{};

  assert(lc_local_accessory_guard_init(&guard, kCatAKey, kCatBKey,
                                       sharedTestHmac, nullptr));
  assert(wronglyKeyedCatB.beginCheckin(300));
  assert(wronglyKeyedCatB.formatRequest(request, sizeof(request)));
  assert(lc_local_accessory_verify(
             &guard, reinterpret_cast<const std::uint8_t *>(request),
             std::strlen(request), &event) ==
         LC_LOCAL_ACCESSORY_AUTH_FAILED);
}

}  // namespace

int main() {
  acceptedRoundTripUsesTheSameWireContract();
  aLostAckRetransmissionClosesWithDuplicateStatus();
  deviceKeysCannotCrossAuthenticate();
  return 0;
}
