#ifndef LIVING_CANVAS_LC_ESPHOME_PRESENCE_H
#define LIVING_CANVAS_LC_ESPHOME_PRESENCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LC_ESPHOME_PRESENCE_LINE_MAX 512u

typedef struct
{
  uint8_t line[LC_ESPHOME_PRESENCE_LINE_MAX];
  uint16_t used;
  bool oversized;
} lc_esphome_presence_t;

typedef void (*lc_esphome_presence_callback_t)(void *context, bool detected);

void lc_esphome_presence_init(lc_esphome_presence_t *parser);
size_t lc_esphome_presence_feed(
  lc_esphome_presence_t *parser,
  const uint8_t *bytes,
  size_t length,
  lc_esphome_presence_callback_t callback,
  void *context);

#endif
