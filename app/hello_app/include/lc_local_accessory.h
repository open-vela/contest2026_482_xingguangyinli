#ifndef LIVING_CANVAS_LC_LOCAL_ACCESSORY_H
#define LIVING_CANVAS_LC_LOCAL_ACCESSORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LC_LOCAL_ACCESSORY_DEVICE_ID_MAX 6u
#define LC_LOCAL_ACCESSORY_SESSION_MAX 24u
#define LC_LOCAL_ACCESSORY_PACKET_MAX 160u
#define LC_LOCAL_ACCESSORY_KEY_BYTES 32u
#define LC_LOCAL_ACCESSORY_TAG_BYTES 32u
#define LC_LOCAL_ACCESSORY_TAG_HEX_LENGTH 64u

typedef enum
{
  LC_LOCAL_ACCESSORY_CAT_A = 0,
  LC_LOCAL_ACCESSORY_CAT_B,
  LC_LOCAL_ACCESSORY_DEVICE_COUNT
} lc_local_accessory_device_t;

typedef enum
{
  LC_LOCAL_ACCESSORY_ACCEPTED = 0,
  LC_LOCAL_ACCESSORY_DUPLICATE,
  LC_LOCAL_ACCESSORY_INVALID_ARGUMENT,
  LC_LOCAL_ACCESSORY_MALFORMED,
  LC_LOCAL_ACCESSORY_AUTH_FAILED,
  LC_LOCAL_ACCESSORY_CRYPTO_FAILED
} lc_local_accessory_status_t;

typedef bool (*lc_local_accessory_hmac_fn)(
  void *context,
  const uint8_t key[LC_LOCAL_ACCESSORY_KEY_BYTES],
  const uint8_t *message,
  size_t message_length,
  uint8_t output[LC_LOCAL_ACCESSORY_TAG_BYTES]);

typedef struct
{
  lc_local_accessory_device_t device;
  char device_id[LC_LOCAL_ACCESSORY_DEVICE_ID_MAX];
  char session_id[LC_LOCAL_ACCESSORY_SESSION_MAX];
  uint32_t sequence;
} lc_local_accessory_event_t;

typedef struct
{
  uint8_t key[LC_LOCAL_ACCESSORY_KEY_BYTES];
  char session_id[LC_LOCAL_ACCESSORY_SESSION_MAX];
  uint32_t last_sequence;
  bool valid;
} lc_local_accessory_cursor_t;

typedef struct
{
  lc_local_accessory_cursor_t cursor[LC_LOCAL_ACCESSORY_DEVICE_COUNT];
  lc_local_accessory_hmac_fn hmac;
  void *hmac_context;
  bool initialized;
} lc_local_accessory_guard_t;

bool lc_local_accessory_guard_init(
  lc_local_accessory_guard_t *guard,
  const uint8_t cat_a_key[LC_LOCAL_ACCESSORY_KEY_BYTES],
  const uint8_t cat_b_key[LC_LOCAL_ACCESSORY_KEY_BYTES],
  lc_local_accessory_hmac_fn hmac,
  void *hmac_context);

bool lc_local_accessory_request_format(
  const char *device_id,
  const char *session_id,
  uint32_t sequence,
  const uint8_t key[LC_LOCAL_ACCESSORY_KEY_BYTES],
  lc_local_accessory_hmac_fn hmac,
  void *hmac_context,
  char *buffer,
  size_t buffer_size);

lc_local_accessory_status_t lc_local_accessory_verify(
  lc_local_accessory_guard_t *guard,
  const uint8_t *packet,
  size_t packet_length,
  lc_local_accessory_event_t *event);

bool lc_local_accessory_ack_format(
  const lc_local_accessory_guard_t *guard,
  const lc_local_accessory_event_t *event,
  lc_local_accessory_status_t status,
  char *buffer,
  size_t buffer_size);

bool lc_local_accessory_constant_time_equal(const uint8_t *left,
                                             const uint8_t *right,
                                             size_t length);

bool lc_local_accessory_mbedtls_hmac(
  void *context,
  const uint8_t key[LC_LOCAL_ACCESSORY_KEY_BYTES],
  const uint8_t *message,
  size_t message_length,
  uint8_t output[LC_LOCAL_ACCESSORY_TAG_BYTES]);

#endif
