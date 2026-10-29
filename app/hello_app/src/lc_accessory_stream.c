#include "lc_accessory_stream.h"

#include <string.h>

static void reset_line(lc_accessory_stream_t *stream)
{
  stream->length = 0u;
  stream->overflow = false;
}
void lc_accessory_stream_init(lc_accessory_stream_t *stream)
{
  if (stream != NULL)
    {
      memset(stream, 0, sizeof(*stream));
    }
}
lc_accessory_stream_status_t lc_accessory_stream_feed(
  lc_accessory_stream_t *stream,
  uint8_t byte,
  lc_accessory_event_t *event)
{
  lc_accessory_parse_status_t parsed;

  if (stream == NULL || event == NULL)
    {
      return LC_ACCESSORY_STREAM_INVALID;
    }

  if (byte == '\r')
    {
      return LC_ACCESSORY_STREAM_NONE;
    }

  if (byte == '\n')
    {
      if (stream->overflow || stream->length == 0u)
        {
          reset_line(stream);
          return LC_ACCESSORY_STREAM_DROPPED;
        }

      stream->buffer[stream->length] = '\0';
      parsed = lc_accessory_event_parse(stream->buffer, event);
      reset_line(stream);
      return parsed == LC_ACCESSORY_OK ? LC_ACCESSORY_STREAM_EVENT
                                      : LC_ACCESSORY_STREAM_INVALID;
    }

  if (stream->overflow)
    {
      return LC_ACCESSORY_STREAM_NONE;
    }

  if (byte < 0x20u || byte > 0x7eu ||
      stream->length + 1u >= sizeof(stream->buffer))
    {
      stream->overflow = true;
      return LC_ACCESSORY_STREAM_NONE;
    }

  stream->buffer[stream->length++] = (char)byte;
  return LC_ACCESSORY_STREAM_NONE;
}
