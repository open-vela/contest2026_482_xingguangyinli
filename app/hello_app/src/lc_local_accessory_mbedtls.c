#include "lc_local_accessory.h"

#include <mbedtls/md.h>

bool lc_local_accessory_mbedtls_hmac(
  void *context,
  const uint8_t key[LC_LOCAL_ACCESSORY_KEY_BYTES],
  const uint8_t *message,
  size_t message_length,
  uint8_t output[LC_LOCAL_ACCESSORY_TAG_BYTES])
{
  const mbedtls_md_info_t *sha256;

  (void)context;
  if (key == NULL || message == NULL || output == NULL)
    {
      return false;
    }

  sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (sha256 == NULL)
    {
      return false;
    }

  return mbedtls_md_hmac(sha256,
                         key, LC_LOCAL_ACCESSORY_KEY_BYTES,
                         message, message_length,
                         output) == 0;
}
