#include "lc_mr60_presence.h"
#include "lc_presence_flow.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned int calls;
static bool values[16];

static void observed(void *context, bool detected)
{
  assert(context == &calls);
  assert(calls < sizeof(values) / sizeof(values[0]));
  values[calls++] = detected;
}

/* Known vendor 0x0F09 one-byte reports; independent hand-calculated checksums. */
static const uint8_t present[] = {1, 0, 1, 0, 1, 0x0f, 9, 0xf8, 1, 0xfe};
static const uint8_t absent[] = {1, 0, 1, 0, 1, 0x0f, 9, 0xf8, 0, 0xff};

static uint8_t checksum(const uint8_t *bytes, size_t length)
{
  uint8_t sum = 0xff;
  while (length-- != 0u) sum ^= *bytes++;
  return sum;
}

typedef struct
{
  lc_presence_tracker_t tracker;
  lc_state_machine_t state;
  uint64_t now;
  unsigned int greetings;
} arrival_t;

static void arrival(void *context, bool detected)
{
  arrival_t *flow = context;
  lc_presence_observation_t observation;
  assert(lc_presence_from_binary(LC_PRESENCE_SOURCE_MMWAVE, detected,
    flow->now, &observation));
  lc_actions_t action = lc_presence_flow_handle(&flow->tracker, &flow->state, &observation);
  assert((action & (LC_ACTION_START_CAPTURE | LC_ACTION_SHOW_QR |
    LC_ACTION_SAVE_CONFIRMED_MEMORY | LC_ACTION_REQUEST_DINNER)) == 0u);
  if (action & LC_ACTION_PLAY_GREETING) ++flow->greetings;
}

static void test_real_presence_state_flow(void)
{
  lc_mr60_presence_t parser;
  arrival_t flow;
  memset(&flow, 0, sizeof(flow));
  lc_mr60_presence_init(&parser);
  lc_presence_tracker_init(&flow.tracker, 70, 500);
  lc_state_init(&flow.state, 30000);
  for (unsigned int cycle = 0; cycle < 5; ++cycle)
    {
      flow.now = 1000u + (uint64_t)cycle * 31000u;
      assert(lc_mr60_presence_feed(&parser, present, sizeof(present), arrival, &flow) == 1);
      assert(flow.greetings == cycle + 1u && flow.state.state == LC_STATE_GREETING);
      assert(lc_state_handle(&flow.state, LC_EVENT_GREETING_FINISHED, flow.now + 100u)
        == LC_ACTION_SHOW_STATE);
      assert(flow.state.state == LC_STATE_IDLE);
      flow.now += 200u;
      assert(lc_mr60_presence_feed(&parser, present, sizeof(present), arrival, &flow) == 1);
      assert(flow.greetings == cycle + 1u); /* Staying must never repeat the greeting. */
      assert(lc_mr60_presence_feed(&parser, NULL, 0, arrival, &flow) == 0);
      assert(flow.tracker.present); /* Missing reports must not fabricate departure. */
      flow.now += 500u;
      assert(lc_mr60_presence_feed(&parser, absent, sizeof(absent), arrival, &flow) == 1);
      assert(!flow.tracker.present && flow.greetings == cycle + 1u);
    }
}

int main(void)
{
  lc_mr60_presence_t parser;
  lc_mr60_presence_init(&parser);
  assert(sizeof(parser) <= 32u); /* No buffer for vitals, point clouds or heap. */
  assert(lc_mr60_presence_feed(&parser, NULL, 0, observed, &calls) == 0u);
  assert(calls == 0u); /* No new frame is unknown, never an absence update. */
  for (size_t split = 0; split <= sizeof(present); ++split)
    {
      lc_mr60_presence_init(&parser);
      calls = 0;
      size_t first = lc_mr60_presence_feed(&parser, present, split, observed, &calls);
      size_t second = lc_mr60_presence_feed(&parser, present + split,
        sizeof(present) - split, observed, &calls);
      assert(first + second == 1u && calls == 1u && values[0]);
    }
  lc_mr60_presence_init(&parser);
  calls = 0;
  assert(lc_mr60_presence_feed(&parser, present, sizeof(present), observed, &calls) == 1u);
  assert(lc_mr60_presence_feed(&parser, absent, sizeof(absent), observed, &calls) == 1u);
  assert(calls == 2u && values[0] && !values[1]);

  /* Every corrupted byte is rejected, followed by recovery at a valid report. */
  for (size_t offset = 0; offset < sizeof(present); ++offset)
    {
      uint8_t bad[sizeof(present)];
      memcpy(bad, present, sizeof(bad));
      bad[offset] ^= 0x80u;
      lc_mr60_presence_init(&parser);
      calls = 0;
      assert(lc_mr60_presence_feed(&parser, bad, sizeof(bad), observed, &calls) == 0u);
      assert(lc_mr60_presence_feed(&parser, absent, sizeof(absent), observed, &calls) == 1u);
      assert(calls == 1u && !values[0]);
    }

  /* Unknown status values, wrong lengths and giant declared payloads fail closed. */
  uint8_t invalid_value[sizeof(present)];
  memcpy(invalid_value, present, sizeof(present));
  invalid_value[8] = 2;
  invalid_value[9] = 0xfd;
  calls = 0;
  lc_mr60_presence_init(&parser);
  assert(lc_mr60_presence_feed(&parser, invalid_value, sizeof(invalid_value), observed, &calls) == 0);
  uint8_t empty[] = {1, 0, 1, 0, 0, 0x0f, 9, 0xf9, 0xff};
  assert(lc_mr60_presence_feed(&parser, empty, sizeof(empty), observed, &calls) == 0);
  uint8_t giant[] = {1, 0, 1, 0xff, 0xff, 0x0f, 9, 0, 0};
  giant[7] = checksum(giant, 7);
  assert(lc_mr60_presence_feed(&parser, giant, sizeof(giant), observed, &calls) == 0);
  assert(lc_mr60_presence_feed(&parser, present, sizeof(present), observed, &calls) == 1);
  assert(calls == 1 && values[0]);

  /* Ignore heart/pointcloud payloads even if they contain a complete presence frame. */
  uint8_t private_frame[8 + 512 + 1];
  memset(private_frame, 0xa5, sizeof(private_frame));
  const uint8_t private_header[] = {1, 0, 7, 2, 0, 0x0a, 0x15, 0};
  memcpy(private_frame, private_header, 8);
  private_frame[7] = checksum(private_frame, 7);
  memcpy(private_frame + 8 + 200, present, sizeof(present));
  private_frame[520] = checksum(private_frame + 8, 512);
  lc_mr60_presence_init(&parser);
  calls = 0;
  for (size_t i = 0; i < sizeof(private_frame); ++i)
    assert(lc_mr60_presence_feed(&parser, private_frame + i, 1, observed, &calls) == 0);
  assert(calls == 0u);
  assert(lc_mr60_presence_feed(&parser, absent, sizeof(absent), observed, &calls) == 1);
  assert(calls == 1 && !values[0]);

  /* Incomplete capture/disconnection must be reset, not reused as fresh data. */
  calls = 0;
  lc_mr60_presence_init(&parser);
  assert(lc_mr60_presence_feed(&parser, present, 9, observed, &calls) == 0);
  lc_mr60_presence_init(&parser);
  assert(lc_mr60_presence_feed(&parser, present + 9, 1, observed, &calls) == 0);
  assert(calls == 0);
  assert(lc_mr60_presence_feed(&parser, present, sizeof(present), observed, &calls) == 1);
  assert(lc_mr60_presence_feed(NULL, present, sizeof(present), observed, &calls) == 0);
  lc_mr60_presence_init(&parser);
  assert(lc_mr60_presence_feed(&parser, present, sizeof(present), NULL, NULL) == 0);
  test_real_presence_state_flow();
  puts("PASS: MR60 fresh presence only, packet checks, split reads, privacy skip and reset");
  return 0;
}
