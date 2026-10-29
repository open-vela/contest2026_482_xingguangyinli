#include "lc_local_accessory.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint8_t CAT_A_TEST_KEY[LC_LOCAL_ACCESSORY_KEY_BYTES] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
  0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};

static const uint8_t CAT_B_TEST_KEY[LC_LOCAL_ACCESSORY_KEY_BYTES] = {
  0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
  0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
  0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
  0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f
};

/* Deterministic host-test stand-in. Production uses the mbedTLS adapter. */
static bool test_hmac(void *context,
                      const uint8_t key[LC_LOCAL_ACCESSORY_KEY_BYTES],
                      const uint8_t *message,
                      size_t message_length,
                      uint8_t output[LC_LOCAL_ACCESSORY_TAG_BYTES])
{
  uint32_t state = context == NULL ? 2166136261u : 0u;
  size_t index;

  for (index = 0u; index < LC_LOCAL_ACCESSORY_KEY_BYTES; index++)
    {
      state = (state ^ key[index]) * 16777619u;
    }
  for (index = 0u; index < message_length; index++)
    {
      state = (state ^ message[index]) * 16777619u;
    }
  for (index = 0u; index < LC_LOCAL_ACCESSORY_TAG_BYTES; index++)
    {
      state ^= state >> 13;
      state *= 0x5bd1e995u;
      output[index] = (uint8_t)(state >> ((index % 4u) * 8u));
    }
  return true;
}

static void init_guard(lc_local_accessory_guard_t *guard)
{
  assert(lc_local_accessory_guard_init(guard,
                                       CAT_A_TEST_KEY,
                                       CAT_B_TEST_KEY,
                                       test_hmac,
                                       NULL));
}

static void test_accepts_only_two_authenticated_devices(void)
{
  lc_local_accessory_guard_t guard;
  lc_local_accessory_event_t event;
  char packet[LC_LOCAL_ACCESSORY_PACKET_MAX];
  const char *prefix = "LC2,cat_a,boot_A-1,7,checkin,1,";

  init_guard(&guard);
  assert(lc_local_accessory_request_format("cat_a", "boot_A-1", 7u,
                                           CAT_A_TEST_KEY, test_hmac, NULL,
                                           packet, sizeof(packet)));
  assert(strncmp(packet, prefix, strlen(prefix)) == 0);
  assert(strlen(packet) == strlen(prefix) +
                           LC_LOCAL_ACCESSORY_TAG_HEX_LENGTH);
  assert(lc_local_accessory_verify(&guard,
                                   (const uint8_t *)packet,
                                   strlen(packet),
                                   &event) ==
         LC_LOCAL_ACCESSORY_ACCEPTED);
  assert(event.device == LC_LOCAL_ACCESSORY_CAT_A);
  assert(strcmp(event.device_id, "cat_a") == 0);
  assert(strcmp(event.session_id, "boot_A-1") == 0);
  assert(event.sequence == 7u);

  assert(lc_local_accessory_request_format("cat_b", "boot_B-1", 1u,
                                           CAT_B_TEST_KEY, test_hmac, NULL,
                                           packet, sizeof(packet)));
  assert(lc_local_accessory_verify(&guard,
                                   (const uint8_t *)packet,
                                   strlen(packet),
                                   &event) ==
         LC_LOCAL_ACCESSORY_ACCEPTED);
  assert(event.device == LC_LOCAL_ACCESSORY_CAT_B);

  assert(!lc_local_accessory_request_format("cat_c", "boot_C-1", 1u,
                                            CAT_A_TEST_KEY, test_hmac, NULL,
                                            packet, sizeof(packet)));
}

static void test_has_independent_replay_cursors(void)
{
  lc_local_accessory_guard_t guard;
  lc_local_accessory_event_t event;
  char cat_a[LC_LOCAL_ACCESSORY_PACKET_MAX];
  char cat_b[LC_LOCAL_ACCESSORY_PACKET_MAX];

  init_guard(&guard);
  assert(lc_local_accessory_request_format("cat_a", "same_session", 3u,
                                           CAT_A_TEST_KEY, test_hmac, NULL,
                                           cat_a, sizeof(cat_a)));
  assert(lc_local_accessory_request_format("cat_b", "same_session", 3u,
                                           CAT_B_TEST_KEY, test_hmac, NULL,
                                           cat_b, sizeof(cat_b)));
  assert(lc_local_accessory_verify(&guard, (const uint8_t *)cat_a,
                                   strlen(cat_a), &event) ==
         LC_LOCAL_ACCESSORY_ACCEPTED);
  assert(lc_local_accessory_verify(&guard, (const uint8_t *)cat_a,
                                   strlen(cat_a), &event) ==
         LC_LOCAL_ACCESSORY_DUPLICATE);
  assert(lc_local_accessory_verify(&guard, (const uint8_t *)cat_b,
                                   strlen(cat_b), &event) ==
         LC_LOCAL_ACCESSORY_ACCEPTED);

  assert(lc_local_accessory_request_format("cat_a", "same_session", 2u,
                                           CAT_A_TEST_KEY, test_hmac, NULL,
                                           cat_a, sizeof(cat_a)));
  assert(lc_local_accessory_verify(&guard, (const uint8_t *)cat_a,
                                   strlen(cat_a), &event) ==
         LC_LOCAL_ACCESSORY_DUPLICATE);
  assert(lc_local_accessory_request_format("cat_a", "new_session", 1u,
                                           CAT_A_TEST_KEY, test_hmac, NULL,
                                           cat_a, sizeof(cat_a)));
  assert(lc_local_accessory_verify(&guard, (const uint8_t *)cat_a,
                                   strlen(cat_a), &event) ==
         LC_LOCAL_ACCESSORY_ACCEPTED);
}

static void test_rejects_tampering_and_noncanonical_packets(void)
{
  lc_local_accessory_guard_t guard;
  lc_local_accessory_event_t event;
  char packet[LC_LOCAL_ACCESSORY_PACKET_MAX];
  size_t length;

  init_guard(&guard);
  assert(lc_local_accessory_request_format("cat_a", "boot_A-2", 9u,
                                           CAT_A_TEST_KEY, test_hmac, NULL,
                                           packet, sizeof(packet)));
  length = strlen(packet);

  packet[8] = 'b';
  assert(lc_local_accessory_verify(&guard, (const uint8_t *)packet,
                                   length, &event) ==
         LC_LOCAL_ACCESSORY_AUTH_FAILED);
  packet[8] = 'a';
  packet[length - 1u] = packet[length - 1u] == '0' ? '1' : '0';
  assert(lc_local_accessory_verify(&guard, (const uint8_t *)packet,
                                   length, &event) ==
         LC_LOCAL_ACCESSORY_AUTH_FAILED);
  packet[length - 1u] = 'A';
  assert(lc_local_accessory_verify(&guard, (const uint8_t *)packet,
                                   length, &event) ==
         LC_LOCAL_ACCESSORY_MALFORMED);

  assert(lc_local_accessory_verify(
           &guard,
           (const uint8_t *)"LC2,cat_a,s,0,checkin,1,"
                            "0000000000000000000000000000000000000000000000000000000000000000",
           90u,
           &event) == LC_LOCAL_ACCESSORY_MALFORMED);
  assert(lc_local_accessory_verify(
           &guard,
           (const uint8_t *)"LC2,cat_a,s,1,1,checkin,"
                            "0000000000000000000000000000000000000000000000000000000000000000",
           90u,
           &event) == LC_LOCAL_ACCESSORY_MALFORMED);
}

static void test_formats_bound_authenticated_ack(void)
{
  lc_local_accessory_guard_t guard;
  lc_local_accessory_event_t event;
  char packet[LC_LOCAL_ACCESSORY_PACKET_MAX];
  char ack[LC_LOCAL_ACCESSORY_PACKET_MAX];
  const char *prefix = "LC2ACK,cat_b,boot_B-9,12,accepted,";

  init_guard(&guard);
  assert(lc_local_accessory_request_format("cat_b", "boot_B-9", 12u,
                                           CAT_B_TEST_KEY, test_hmac, NULL,
                                           packet, sizeof(packet)));
  assert(lc_local_accessory_verify(&guard, (const uint8_t *)packet,
                                   strlen(packet), &event) ==
         LC_LOCAL_ACCESSORY_ACCEPTED);
  assert(lc_local_accessory_ack_format(&guard, &event,
                                       LC_LOCAL_ACCESSORY_ACCEPTED,
                                       ack, sizeof(ack)));
  assert(strncmp(ack, prefix, strlen(prefix)) == 0);
  assert(strlen(ack) == strlen(prefix) + LC_LOCAL_ACCESSORY_TAG_HEX_LENGTH);
  assert(lc_local_accessory_ack_format(&guard, &event,
                                       LC_LOCAL_ACCESSORY_DUPLICATE,
                                       ack, sizeof(ack)));
  assert(strstr(ack, ",duplicate,") != NULL);
  assert(!lc_local_accessory_ack_format(&guard, &event,
                                        LC_LOCAL_ACCESSORY_MALFORMED,
                                        ack, sizeof(ack)));
}

static void test_constant_time_comparison_contract(void)
{
  uint8_t left[LC_LOCAL_ACCESSORY_TAG_BYTES] = {0};
  uint8_t right[LC_LOCAL_ACCESSORY_TAG_BYTES] = {0};

  assert(lc_local_accessory_constant_time_equal(left, right, sizeof(left)));
  right[0] = 1u;
  assert(!lc_local_accessory_constant_time_equal(left, right, sizeof(left)));
  right[0] = 0u;
  right[LC_LOCAL_ACCESSORY_TAG_BYTES - 1u] = 1u;
  assert(!lc_local_accessory_constant_time_equal(left, right, sizeof(left)));
  assert(!lc_local_accessory_constant_time_equal(NULL, right, sizeof(left)));
}

int main(void)
{
  test_accepts_only_two_authenticated_devices();
  test_has_independent_replay_cursors();
  test_rejects_tampering_and_noncanonical_packets();
  test_formats_bound_authenticated_ack();
  test_constant_time_comparison_contract();
  puts("lc_local_accessory tests passed");
  return 0;
}
