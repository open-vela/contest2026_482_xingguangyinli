#ifndef LC_MR60_PRESENCE_H
#define LC_MR60_PRESENCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* MR60BHA2 raw UART frame decoder, not an ESPHome USB-log decoder. No I/O,
 * commands, configuration, firmware update, biometric values or heap storage.
 * Reset on disconnect/partial-frame timeout. One caller per decoder; callback
 * must not re-enter it. Unknown/no fresh packet never means "nobody present".
 * Presence is an observation, not an authenticated identity or purchase action.
 */
typedef struct
{
  uint8_t header[8];
  uint8_t used;
  uint8_t phase;
  uint8_t presence;
  uint16_t discard;
} lc_mr60_presence_t;

typedef void (*lc_mr60_presence_callback_t)(void *context, bool detected);
void lc_mr60_presence_init(lc_mr60_presence_t *parser);
size_t lc_mr60_presence_feed(lc_mr60_presence_t *parser,
  const uint8_t *bytes, size_t length, lc_mr60_presence_callback_t callback,
  void *context);

#endif
