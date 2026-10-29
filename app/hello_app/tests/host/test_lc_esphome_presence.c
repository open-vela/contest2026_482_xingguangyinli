#include "lc_esphome_presence.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool values[16];
static size_t calls;

static void observed(void *context, bool detected)
{
  assert(context == &calls);
  assert(calls < sizeof(values) / sizeof(values[0]));
  values[calls++] = detected;
}

static size_t feed_text(lc_esphome_presence_t *parser, const char *text)
{
  return lc_esphome_presence_feed(parser, (const uint8_t *)text,
                                  strlen(text), observed, &calls);
}

static void test_exact_reports_and_split_reads(void)
{
  static const char present[] =
    "[12:34:56.123][D][sensor:123]: 'Target number': Sending state 2.00000 "
    "with 5 decimals of accuracy\n";
  static const char absent[] =
    "\x1b[0;36m[D][sensor:7]: 'Target number': Sending state 0 with 0 "
    "decimals of accuracy\x1b[0m\r\n";
  lc_esphome_presence_t parser;

  for (size_t split = 0u; split <= sizeof(present) - 1u; ++split)
    {
      lc_esphome_presence_init(&parser);
      calls = 0u;
      size_t first = lc_esphome_presence_feed(
        &parser, (const uint8_t *)present, split, observed, &calls);
      size_t second = lc_esphome_presence_feed(
        &parser, (const uint8_t *)present + split,
        sizeof(present) - 1u - split, observed, &calls);
      assert(first + second == 1u);
      assert(calls == 1u && values[0]);
    }

  lc_esphome_presence_init(&parser);
  calls = 0u;
  assert(feed_text(&parser, absent) == 1u);
  assert(calls == 1u && !values[0]);
}

static void test_unrelated_or_malformed_lines_fail_closed(void)
{
  static const char *invalid[] = {
    "[D][sensor:7]: 'Distance': Sending state 1 with 0 decimals of accuracy\n",
    "[D][sensor:7]: 'Target number': Sending state 256 with 0 decimals of accuracy\n",
    "[D][sensor:7]: 'Target number': Sending state 1.10000 with 5 decimals of accuracy\n",
    "[D][sensor:7]: 'Target number': Sending state 1 with 6 decimals of accuracy\n",
    "[D][sensor:7]: 'Target number': Sending state 1 with 0 decimals of accuracy extra\n",
    "[I][sensor:7]: 'Target number': Sending state 1 with 0 decimals of accuracy\n",
    "[D][sensor:x]: 'Target number': Sending state 1 with 0 decimals of accuracy\n",
    "[D][sensor:7]: 'Target number': Sending state 1 with 0 decimals of accuracy",
  };
  lc_esphome_presence_t parser;

  for (size_t index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index)
    {
      lc_esphome_presence_init(&parser);
      calls = 0u;
      assert(feed_text(&parser, invalid[index]) == 0u);
      assert(calls == 0u);
    }
}

static void test_oversize_recovery_and_arguments(void)
{
  static const char valid[] =
    "[D][sensor:42]: 'Target number': Sending state 1 with 0 decimals of accuracy\n";
  uint8_t oversized[LC_ESPHOME_PRESENCE_LINE_MAX + 2u];
  lc_esphome_presence_t parser;

  memset(oversized, 'A', sizeof(oversized));
  oversized[sizeof(oversized) - 1u] = '\n';
  lc_esphome_presence_init(&parser);
  calls = 0u;
  assert(lc_esphome_presence_feed(&parser, oversized, sizeof(oversized),
                                  observed, &calls) == 0u);
  assert(feed_text(&parser, valid) == 1u);
  assert(calls == 1u && values[0]);

  assert(lc_esphome_presence_feed(NULL, (const uint8_t *)valid,
                                  sizeof(valid) - 1u, observed,
                                  &calls) == 0u);
  assert(lc_esphome_presence_feed(&parser, NULL, 1u, observed,
                                  &calls) == 0u);
  assert(lc_esphome_presence_feed(&parser, (const uint8_t *)valid,
                                  sizeof(valid) - 1u, NULL, NULL) == 0u);
}

int main(void)
{
  test_exact_reports_and_split_reads();
  test_unrelated_or_malformed_lines_fail_closed();
  test_oversize_recovery_and_arguments();
  puts("PASS: strict ESPHome target-count presence parser");
  return 0;
}
