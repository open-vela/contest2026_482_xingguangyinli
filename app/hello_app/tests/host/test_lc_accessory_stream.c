#include "lc_accessory_stream.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static lc_accessory_stream_status_t feed_line(
  lc_accessory_stream_t *stream,
  const char *line,
  lc_accessory_event_t *event)
{
  size_t index;
  lc_accessory_stream_status_t status = LC_ACCESSORY_STREAM_NONE;

  for (index = 0u; line[index] != '\0'; index++)
    {
      status = lc_accessory_stream_feed(stream, (unsigned char)line[index],
                                        event);
      assert(status == LC_ACCESSORY_STREAM_NONE);
    }

  return lc_accessory_stream_feed(stream, '\n', event);
}

static void test_fragmented_crlf_event(void)
{
  lc_accessory_stream_t stream;
  lc_accessory_event_t event;
  const char *wire =
    "LC1,mmwave_bridge,presence_demo,7,presence,1\r";

  lc_accessory_stream_init(&stream);
  assert(feed_line(&stream, wire, &event) == LC_ACCESSORY_STREAM_EVENT);
  assert(event.type == LC_ACCESSORY_PRESENCE);
  assert(event.value == 1);
  assert(event.sequence == 7u);
}

static void test_two_lines_and_malformed_recovery(void)
{
  lc_accessory_stream_t stream;
  lc_accessory_event_t event;

  lc_accessory_stream_init(&stream);
  assert(feed_line(&stream, "broken", &event) ==
         LC_ACCESSORY_STREAM_INVALID);
  assert(feed_line(&stream,
                   "LC1,camera_bridge,presence_demo,8,presence,0",
                   &event) == LC_ACCESSORY_STREAM_EVENT);
  assert(event.value == 0);
  assert(event.sequence == 8u);
}

static void test_overflow_is_dropped_and_next_line_recovers(void)
{
  lc_accessory_stream_t stream;
  lc_accessory_event_t event;
  size_t index;

  lc_accessory_stream_init(&stream);
  for (index = 0u; index < LC_ACCESSORY_WIRE_MAX + 10u; index++)
    {
      assert(lc_accessory_stream_feed(&stream, 'X', &event) ==
             LC_ACCESSORY_STREAM_NONE);
    }

  assert(lc_accessory_stream_feed(&stream, '\n', &event) ==
         LC_ACCESSORY_STREAM_DROPPED);
  assert(feed_line(&stream,
                   "LC1,mmwave_bridge,presence_demo,9,presence,1",
                   &event) == LC_ACCESSORY_STREAM_EVENT);
  assert(event.sequence == 9u);
}

int main(void)
{
  test_fragmented_crlf_event();
  test_two_lines_and_malformed_recovery();
  test_overflow_is_dropped_and_next_line_recovers();
  puts("PASS: lc_accessory_stream");
  return 0;
}
