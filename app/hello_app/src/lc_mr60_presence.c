#include "lc_mr60_presence.h"
#include <string.h>

/* Wire facts checked against Seeed's SeeedmmWave.{h,cpp} and
 * SEEED_MR60BHA2.{h,cpp}, shipped v1.0.0/main (2026-10-13).
 * Header: 01, id BE16, length BE16, type BE16, complemented-XOR checksum.
 * Payload checksum uses the same algorithm. Only type 0x0F09, length1,
 * value0/1 is a human-presence report. Other data is drained, never retained.
 */
enum { HEADER, PRESENCE, CHECKSUM, DISCARD };

void lc_mr60_presence_init(lc_mr60_presence_t *parser)
{
  if (parser != NULL) memset(parser, 0, sizeof(*parser));
}

static void resync(lc_mr60_presence_t *parser)
{
  /* Keep only a possible frame-header prefix; no payload is scanned as frames. */
  for (uint8_t i = 1u; i < parser->used; ++i)
    if (parser->header[i] == 1u)
      {
        parser->used = (uint8_t)(parser->used - i);
        memmove(parser->header, parser->header + i, parser->used);
        memset(parser->header + parser->used, 0, sizeof(parser->header) - parser->used);
        return;
      }
  lc_mr60_presence_init(parser);
}

size_t lc_mr60_presence_feed(lc_mr60_presence_t *parser,
  const uint8_t *bytes, size_t length, lc_mr60_presence_callback_t callback,
  void *context)
{
  if (parser == NULL || callback == NULL || (bytes == NULL && length != 0u))
    return 0u;
  size_t reports = 0u;
  for (size_t index = 0u; index < length; ++index)
    {
      uint8_t byte = bytes[index];
      if (parser->phase == DISCARD)
        {
          if (--parser->discard == 0u) lc_mr60_presence_init(parser);
          continue;
        }
      if (parser->phase == PRESENCE)
        { parser->presence = byte; parser->phase = CHECKSUM; continue; }
      if (parser->phase == CHECKSUM)
        {
          uint8_t value = parser->presence;
          uint8_t expected = (uint8_t)(value ^ 0xffu);
          lc_mr60_presence_init(parser);
          if (value <= 1u && byte == expected)
            { callback(context, value == 1u); ++reports; }
          continue;
        }
      if (parser->used == 0u && byte != 1u) continue;
      parser->header[parser->used++] = byte;
      if (parser->used != sizeof(parser->header)) continue;
      uint8_t checksum = 0xffu;
      for (size_t i = 0u; i < 7u; ++i) checksum ^= parser->header[i];
      uint16_t payload = (uint16_t)((uint16_t)parser->header[3] << 8) | parser->header[4];
      if (checksum != parser->header[7] || payload > 512u)
        { resync(parser); continue; }
      bool presence_report = parser->header[5] == 0x0fu &&
        parser->header[6] == 0x09u && payload == 1u;
      lc_mr60_presence_init(parser);
      if (presence_report) parser->phase = PRESENCE;
      else
        { parser->phase = DISCARD; parser->discard = (uint16_t)(payload + 1u); }
    }
  return reports;
}
