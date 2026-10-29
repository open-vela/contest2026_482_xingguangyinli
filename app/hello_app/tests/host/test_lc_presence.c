#include "lc_presence.h"

#include <assert.h>
#include <stdio.h>

static lc_presence_observation_t observation(lc_presence_source_t source,
                                             bool detected,
                                             unsigned int confidence,
                                             uint64_t now_ms)
{
  lc_presence_observation_t value;

  value.source = source;
  value.detected = detected;
  value.confidence_percent = (uint8_t)confidence;
  value.observed_ms = now_ms;
  return value;
}
static void test_confident_detection_enters_then_stays(void)
{
  lc_presence_tracker_t tracker;
  lc_presence_observation_t first =
    observation(LC_PRESENCE_SOURCE_MMWAVE, true, 90u, 1000u);
  lc_presence_observation_t second =
    observation(LC_PRESENCE_SOURCE_CAMERA, true, 82u, 1200u);

  lc_presence_tracker_init(&tracker, 70u, 1500u);
  assert(lc_presence_update(&tracker, &first) == LC_PRESENCE_ENTER);
  assert(tracker.present);
  assert(tracker.last_source == LC_PRESENCE_SOURCE_MMWAVE);
  assert(lc_presence_update(&tracker, &second) == LC_PRESENCE_STAY);
  assert(tracker.last_source == LC_PRESENCE_SOURCE_CAMERA);
}
static void test_low_confidence_is_ignored(void)
{
  lc_presence_tracker_t tracker;
  lc_presence_observation_t weak =
    observation(LC_PRESENCE_SOURCE_CAMERA, true, 69u, 1000u);

  lc_presence_tracker_init(&tracker, 70u, 1500u);
  assert(lc_presence_update(&tracker, &weak) == LC_PRESENCE_NONE);
  assert(!tracker.present);
}

static void test_leave_requires_grace_period(void)
{
  lc_presence_tracker_t tracker;
  lc_presence_observation_t enter =
    observation(LC_PRESENCE_SOURCE_MMWAVE, true, 100u, 1000u);
  lc_presence_observation_t absent_early =
    observation(LC_PRESENCE_SOURCE_MMWAVE, false, 0u, 2000u);
  lc_presence_observation_t absent_late =
    observation(LC_PRESENCE_SOURCE_MMWAVE, false, 0u, 2500u);

  lc_presence_tracker_init(&tracker, 70u, 1500u);
  assert(lc_presence_update(&tracker, &enter) == LC_PRESENCE_ENTER);
  assert(lc_presence_update(&tracker, &absent_early) == LC_PRESENCE_NONE);
  assert(tracker.present);
  assert(lc_presence_update(&tracker, &absent_late) == LC_PRESENCE_LEAVE);
  assert(!tracker.present);
}

static void test_explicit_absence_completes_after_grace_without_repeat(void)
{
  lc_presence_tracker_t tracker;
  lc_presence_observation_t enter =
    observation(LC_PRESENCE_SOURCE_MMWAVE, true, 100u, 1000u);
  lc_presence_observation_t absent =
    observation(LC_PRESENCE_SOURCE_MMWAVE, false, 0u, 1000u);

  lc_presence_tracker_init(&tracker, 70u, 1200u);
  assert(lc_presence_update(&tracker, &enter) == LC_PRESENCE_ENTER);
  assert(lc_presence_update(&tracker, &absent) == LC_PRESENCE_NONE);
  assert(lc_presence_tick(&tracker, 2199u) == LC_PRESENCE_NONE);
  assert(tracker.present);
  assert(lc_presence_tick(&tracker, 2200u) == LC_PRESENCE_LEAVE);
  assert(!tracker.present);
  assert(lc_presence_tick(&tracker, 5000u) == LC_PRESENCE_NONE);
}

static void test_silence_never_becomes_absence(void)
{
  lc_presence_tracker_t tracker;
  lc_presence_observation_t enter =
    observation(LC_PRESENCE_SOURCE_MMWAVE, true, 100u, 1000u);

  lc_presence_tracker_init(&tracker, 70u, 1200u);
  assert(lc_presence_update(&tracker, &enter) == LC_PRESENCE_ENTER);
  assert(lc_presence_tick(&tracker, 60000u) == LC_PRESENCE_NONE);
  assert(tracker.present);
}

static void test_new_presence_cancels_pending_absence(void)
{
  lc_presence_tracker_t tracker;
  lc_presence_observation_t enter =
    observation(LC_PRESENCE_SOURCE_MMWAVE, true, 100u, 1000u);
  lc_presence_observation_t absent =
    observation(LC_PRESENCE_SOURCE_MMWAVE, false, 0u, 1100u);
  lc_presence_observation_t recovered =
    observation(LC_PRESENCE_SOURCE_MMWAVE, true, 100u, 1150u);

  lc_presence_tracker_init(&tracker, 70u, 1200u);
  assert(lc_presence_update(&tracker, &enter) == LC_PRESENCE_ENTER);
  assert(lc_presence_update(&tracker, &absent) == LC_PRESENCE_NONE);
  assert(lc_presence_update(&tracker, &recovered) == LC_PRESENCE_STAY);
  assert(lc_presence_tick(&tracker, 60000u) == LC_PRESENCE_NONE);
  assert(tracker.present);
}

static void test_clock_rollback_does_not_emit_false_leave(void)
{
  lc_presence_tracker_t tracker;
  lc_presence_observation_t enter =
    observation(LC_PRESENCE_SOURCE_ACCESSORY, true, 100u, 5000u);
  lc_presence_observation_t rollback =
    observation(LC_PRESENCE_SOURCE_ACCESSORY, false, 0u, 100u);

  lc_presence_tracker_init(&tracker, 70u, 1500u);
  assert(lc_presence_update(&tracker, &enter) == LC_PRESENCE_ENTER);
  assert(lc_presence_update(&tracker, &rollback) == LC_PRESENCE_NONE);
  assert(tracker.present);
  assert(tracker.last_seen_ms == 100u);
}

static void test_binary_adapter_is_privacy_minimal(void)
{
  lc_presence_observation_t value;

  assert(lc_presence_from_binary(LC_PRESENCE_SOURCE_MMWAVE, true, 42u,
                                 &value));
  assert(value.detected);
  assert(value.confidence_percent == 100u);
  assert(value.observed_ms == 42u);
  assert(!lc_presence_from_binary((lc_presence_source_t)99, true, 42u,
                                  &value));
}

static void test_invalid_observation_is_rejected(void)
{
  lc_presence_tracker_t tracker;
  lc_presence_observation_t invalid =
    observation(LC_PRESENCE_SOURCE_CAMERA, true, 101u, 1000u);

  lc_presence_tracker_init(&tracker, 70u, 1500u);
  assert(lc_presence_update(&tracker, &invalid) == LC_PRESENCE_INVALID);
  assert(lc_presence_update(NULL, &invalid) == LC_PRESENCE_INVALID);
}

int main(void)
{
  test_confident_detection_enters_then_stays();
  test_low_confidence_is_ignored();
  test_leave_requires_grace_period();
  test_explicit_absence_completes_after_grace_without_repeat();
  test_silence_never_becomes_absence();
  test_new_presence_cancels_pending_absence();
  test_clock_rollback_does_not_emit_false_leave();
  test_binary_adapter_is_privacy_minimal();
  test_invalid_observation_is_rejected();
  puts("PASS: lc_presence");
  return 0;
}
