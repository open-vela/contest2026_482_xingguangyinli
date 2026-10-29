#include "lc_accessory_event.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_parses_bounded_shake_event(void)
{
  lc_accessory_event_t event;

  assert(lc_accessory_event_parse(
           "LC1,token_left,dinner_42,7,shake,1", &event) ==
         LC_ACCESSORY_OK);
  assert(strcmp(event.device_id, "token_left") == 0);
  assert(strcmp(event.session_id, "dinner_42") == 0);
  assert(event.sequence == 7u);
  assert(event.type == LC_ACCESSORY_SHAKE);
  assert(event.value == 1);
}

static void test_parses_explicit_touch_checkin(void)
{
  lc_accessory_event_t event;

  assert(lc_accessory_event_parse(
           "LC1,cat_a,touch_42,1,checkin,1", &event) ==
         LC_ACCESSORY_OK);
  assert(strcmp(event.device_id, "cat_a") == 0);
  assert(event.type == LC_ACCESSORY_CHECKIN);
  assert(event.value == 1);

  assert(lc_accessory_event_parse(
           "LC1,cat_a,touch_42,2,checkin,0", &event) ==
         LC_ACCESSORY_BAD_VALUE);
}

static void test_rejects_malformed_or_unsafe_input(void)
{
  lc_accessory_event_t event;

  assert(lc_accessory_event_parse(
           "LC2,token_left,dinner_42,7,shake,1", &event) ==
         LC_ACCESSORY_BAD_VERSION);
  assert(lc_accessory_event_parse(
           "LC1,bad device,dinner_42,7,shake,1", &event) ==
         LC_ACCESSORY_BAD_ID);
  assert(lc_accessory_event_parse(
           "LC1,token_left,dinner_42,0,shake,1", &event) ==
         LC_ACCESSORY_BAD_SEQUENCE);
  assert(lc_accessory_event_parse(
           "LC1,token_left,dinner_42,7,purchase,1", &event) ==
         LC_ACCESSORY_BAD_TYPE);
  assert(lc_accessory_event_parse(
           "LC1,token_left,dinner_42,7,presence,2", &event) ==
         LC_ACCESSORY_BAD_VALUE);
  assert(lc_accessory_event_parse(
           "LC1,camera_bridge,dinner_42,7,presence_confidence,82", &event) ==
         LC_ACCESSORY_OK);
  assert(event.type == LC_ACCESSORY_PRESENCE_CONFIDENCE);
  assert(event.value == 82);
  assert(lc_accessory_event_parse(
           "LC1,camera_bridge,dinner_42,8,presence_confidence,101", &event) ==
         LC_ACCESSORY_BAD_VALUE);
  assert(lc_accessory_event_parse(
           "LC1,token_left,dinner_42,7,shake,1,extra", &event) ==
         LC_ACCESSORY_MALFORMED);
}

static void test_deduplicates_per_device_and_session(void)
{
  lc_accessory_cursor_t cursor;
  lc_accessory_event_t event;

  lc_accessory_cursor_init(&cursor);
  assert(lc_accessory_event_parse(
           "LC1,token_left,dinner_42,7,shake,1", &event) ==
         LC_ACCESSORY_OK);
  assert(lc_accessory_cursor_accept(&cursor, &event) ==
         LC_ACCESSORY_ACCEPTED);
  assert(lc_accessory_cursor_accept(&cursor, &event) ==
         LC_ACCESSORY_REPLAY);

  assert(lc_accessory_event_parse(
           "LC1,token_left,dinner_42,6,shake,1", &event) ==
         LC_ACCESSORY_OK);
  assert(lc_accessory_cursor_accept(&cursor, &event) ==
         LC_ACCESSORY_REPLAY);

  assert(lc_accessory_event_parse(
           "LC1,token_left,dinner_42,8,confirm,0", &event) ==
         LC_ACCESSORY_OK);
  assert(lc_accessory_cursor_accept(&cursor, &event) ==
         LC_ACCESSORY_ACCEPTED);

  assert(lc_accessory_event_parse(
           "LC1,token_left,dinner_43,1,heartbeat,0", &event) ==
         LC_ACCESSORY_OK);
  assert(lc_accessory_cursor_accept(&cursor, &event) ==
         LC_ACCESSORY_NEW_SESSION);
}

static void test_formats_bounded_acknowledgements(void)
{
  lc_accessory_event_t event;
  char ack[LC_ACCESSORY_WIRE_MAX];
  char short_ack[12];

  assert(lc_accessory_event_parse(
           "LC1,token_left,dinner_42,8,confirm,0", &event) ==
         LC_ACCESSORY_OK);
  assert(lc_accessory_ack_format(&event, LC_ACCESSORY_ACCEPTED,
                                 ack, sizeof(ack)));
  assert(strcmp(ack, "LC1ACK,token_left,dinner_42,8,ok") == 0);

  assert(lc_accessory_ack_format(&event, LC_ACCESSORY_NEW_SESSION,
                                 ack, sizeof(ack)));
  assert(strcmp(ack, "LC1ACK,token_left,dinner_42,8,session") == 0);

  assert(lc_accessory_ack_format(&event, LC_ACCESSORY_REPLAY,
                                 ack, sizeof(ack)));
  assert(strcmp(ack, "LC1ACK,token_left,dinner_42,8,duplicate") == 0);

  assert(lc_accessory_ack_format(&event, LC_ACCESSORY_DEVICE_MISMATCH,
                                 ack, sizeof(ack)));
  assert(strcmp(ack, "LC1ACK,token_left,dinner_42,8,rejected") == 0);

  assert(!lc_accessory_ack_format(&event, LC_ACCESSORY_ACCEPTED,
                                  short_ack, sizeof(short_ack)));
  assert(!lc_accessory_ack_format(NULL, LC_ACCESSORY_ACCEPTED,
                                  ack, sizeof(ack)));
}

int main(void)
{
  test_parses_bounded_shake_event();
  test_parses_explicit_touch_checkin();
  test_rejects_malformed_or_unsafe_input();
  test_deduplicates_per_device_and_session();
  test_formats_bounded_acknowledgements();
  puts("lc_accessory_event tests passed");
  return 0;
}
