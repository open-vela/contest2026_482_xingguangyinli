#include "lc_checkin.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static lc_accessory_event_t event(const char *device_id,
                                  const char *session_id,
                                  uint32_t sequence,
                                  lc_accessory_event_type_t type)
{
  lc_accessory_event_t value;
  char wire[LC_ACCESSORY_WIRE_MAX];
  const char *wire_type = type == LC_ACCESSORY_CHECKIN
                            ? "checkin"
                            : "heartbeat";
  int length;

  length = snprintf(wire, sizeof(wire), "LC1,%s,%s,%lu,%s,%d",
                    device_id, session_id, (unsigned long)sequence,
                    wire_type, type == LC_ACCESSORY_CHECKIN ? 1 : 0);
  assert(length > 0 && (size_t)length < sizeof(wire));
  assert(lc_accessory_event_parse(wire, &value) == LC_ACCESSORY_OK);
  return value;
}

static void accepts_two_independent_touch_checkins(void)
{
  lc_checkin_tracker_t tracker;
  lc_checkin_result_t result;
  lc_accessory_event_t cat_a =
    event("cat_a", "boot_a", 1u, LC_ACCESSORY_CHECKIN);
  lc_accessory_event_t cat_b =
    event("cat_b", "boot_b", 1u, LC_ACCESSORY_CHECKIN);

  lc_checkin_tracker_init(&tracker);
  assert(!lc_checkin_tracker_checked_in(&tracker, LC_CHECKIN_CAT_A));
  assert(!lc_checkin_tracker_checked_in(&tracker, LC_CHECKIN_CAT_B));

  assert(lc_checkin_tracker_accept(&tracker, &cat_a, &result) ==
         LC_CHECKIN_ACCEPTED);
  assert(result.slot == LC_CHECKIN_CAT_A);
  assert(strcmp(result.ack, "LC1ACK,cat_a,boot_a,1,ok") == 0);
  assert(lc_checkin_tracker_checked_in(&tracker, LC_CHECKIN_CAT_A));
  assert(!lc_checkin_tracker_checked_in(&tracker, LC_CHECKIN_CAT_B));

  assert(lc_checkin_tracker_accept(&tracker, &cat_b, &result) ==
         LC_CHECKIN_ACCEPTED);
  assert(result.slot == LC_CHECKIN_CAT_B);
  assert(strcmp(result.ack, "LC1ACK,cat_b,boot_b,1,ok") == 0);
  assert(lc_checkin_tracker_checked_in(&tracker, LC_CHECKIN_CAT_B));
}

static void deduplicates_each_device_without_cross_talk(void)
{
  lc_checkin_tracker_t tracker;
  lc_checkin_result_t result;
  lc_accessory_event_t cat_a =
    event("cat_a", "boot_a", 4u, LC_ACCESSORY_CHECKIN);
  lc_accessory_event_t cat_b =
    event("cat_b", "boot_b", 4u, LC_ACCESSORY_CHECKIN);

  lc_checkin_tracker_init(&tracker);
  assert(lc_checkin_tracker_accept(&tracker, &cat_a, &result) ==
         LC_CHECKIN_ACCEPTED);
  assert(lc_checkin_tracker_accept(&tracker, &cat_a, &result) ==
         LC_CHECKIN_DUPLICATE);
  assert(strcmp(result.ack, "LC1ACK,cat_a,boot_a,4,duplicate") == 0);

  assert(lc_checkin_tracker_accept(&tracker, &cat_b, &result) ==
         LC_CHECKIN_ACCEPTED);
  assert(result.slot == LC_CHECKIN_CAT_B);
}

static void rejects_unknown_or_unrelated_events(void)
{
  lc_checkin_tracker_t tracker;
  lc_checkin_result_t result;
  lc_accessory_event_t unknown =
    event("cat_c", "boot_c", 1u, LC_ACCESSORY_CHECKIN);
  lc_accessory_event_t heartbeat =
    event("cat_a", "boot_a", 1u, LC_ACCESSORY_HEARTBEAT);

  lc_checkin_tracker_init(&tracker);
  assert(lc_checkin_tracker_accept(&tracker, &unknown, &result) ==
         LC_CHECKIN_REJECTED);
  assert(strcmp(result.ack, "LC1ACK,cat_c,boot_c,1,rejected") == 0);
  assert(!lc_checkin_tracker_checked_in(&tracker, LC_CHECKIN_CAT_A));
  assert(!lc_checkin_tracker_checked_in(&tracker, LC_CHECKIN_CAT_B));

  assert(lc_checkin_tracker_accept(&tracker, &heartbeat, &result) ==
         LC_CHECKIN_IGNORED);
  assert(result.ack[0] == '\0');
}

static void formats_small_chinese_screen_status(void)
{
  lc_checkin_tracker_t tracker;
  lc_checkin_result_t result;
  lc_accessory_event_t cat_a =
    event("cat_a", "boot_a", 1u, LC_ACCESSORY_CHECKIN);
  lc_accessory_event_t cat_b =
    event("cat_b", "boot_b", 1u, LC_ACCESSORY_CHECKIN);
  char text[64];
  char too_small[8];

  lc_checkin_tracker_init(&tracker);
  assert(lc_checkin_format_status(&tracker, text, sizeof(text)));
  assert(strcmp(text, "A未打卡 · B未打卡") == 0);

  assert(lc_checkin_tracker_accept(&tracker, &cat_a, &result) ==
         LC_CHECKIN_ACCEPTED);
  assert(lc_checkin_format_status(&tracker, text, sizeof(text)));
  assert(strcmp(text, "A已打卡 · B未打卡") == 0);

  assert(lc_checkin_tracker_accept(&tracker, &cat_b, &result) ==
         LC_CHECKIN_ACCEPTED);
  assert(lc_checkin_format_status(&tracker, text, sizeof(text)));
  assert(strcmp(text, "A已打卡 · B已打卡") == 0);
  assert(!lc_checkin_format_status(&tracker, too_small, sizeof(too_small)));
}

static lc_checkin_status_t feed_line(lc_checkin_runtime_t *runtime,
                                     const char *line,
                                     lc_checkin_result_t *result)
{
  lc_checkin_status_t status = LC_CHECKIN_IGNORED;
  size_t index;

  for (index = 0u; line[index] != '\0'; index++)
    {
      status = lc_checkin_runtime_feed(runtime, (uint8_t)line[index], result);
      assert(status == LC_CHECKIN_IGNORED);
    }

  return lc_checkin_runtime_feed(runtime, '\n', result);
}

static void decodes_fragmented_wire_events_for_both_devices(void)
{
  lc_checkin_runtime_t runtime;
  lc_checkin_result_t result;
  char text[64];

  assert(lc_checkin_runtime_init(&runtime));
  assert(feed_line(&runtime, "LC1,cat_a,boot_a,1,checkin,1", &result) ==
         LC_CHECKIN_ACCEPTED);
  assert(result.slot == LC_CHECKIN_CAT_A);
  assert(feed_line(&runtime, "LC1,cat_b,boot_b,1,checkin,1", &result) ==
         LC_CHECKIN_ACCEPTED);
  assert(result.slot == LC_CHECKIN_CAT_B);
  assert(feed_line(&runtime, "LC1,cat_a,boot_a,1,checkin,1", &result) ==
         LC_CHECKIN_DUPLICATE);
  assert(feed_line(&runtime, "LC1,cat_a,boot_a,2,heartbeat,0", &result) ==
         LC_CHECKIN_IGNORED);
  assert(result.ack[0] == '\0');
  assert(lc_checkin_format_status(&runtime.tracker, text, sizeof(text)));
  assert(strcmp(text, "A已打卡 · B已打卡") == 0);
}

int main(void)
{
  accepts_two_independent_touch_checkins();
  deduplicates_each_device_without_cross_talk();
  rejects_unknown_or_unrelated_events();
  formats_small_chinese_screen_status();
  decodes_fragmented_wire_events_for_both_devices();
  puts("PASS: lc_checkin");
  return 0;
}
