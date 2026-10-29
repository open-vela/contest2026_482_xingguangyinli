#include "lc_accessory_runtime.h"

#include <string.h>

static bool valid_source(lc_presence_source_t source)
{
  return source == LC_PRESENCE_SOURCE_ACCESSORY ||
         source == LC_PRESENCE_SOURCE_MMWAVE ||
         source == LC_PRESENCE_SOURCE_CAMERA;
}

static void reset_result(lc_accessory_runtime_result_t *result)
{
  memset(result, 0, sizeof(*result));
  result->status = LC_ACCESSORY_RUNTIME_NONE;
  result->acceptance = LC_ACCESSORY_CURSOR_INVALID;
}

bool lc_accessory_runtime_init(lc_accessory_runtime_t *runtime,
                               lc_state_machine_t *state,
                               lc_presence_source_t source,
                               uint8_t minimum_confidence,
                               uint32_t leave_grace_ms)
{
  if (runtime == NULL || state == NULL || !valid_source(source) ||
      minimum_confidence > 100u)
    {
      return false;
    }

  memset(runtime, 0, sizeof(*runtime));
  lc_accessory_stream_init(&runtime->stream);
  lc_accessory_cursor_init(&runtime->cursor);
  lc_presence_tracker_init(&runtime->presence, minimum_confidence,
                           leave_grace_ms);
  runtime->state = state;
  runtime->source = source;
  runtime->initialized = true;
  return true;
}

lc_accessory_runtime_status_t lc_accessory_runtime_feed(
  lc_accessory_runtime_t *runtime,
  uint8_t byte,
  uint64_t observed_ms,
  lc_accessory_runtime_result_t *result)
{
  lc_accessory_stream_status_t stream_status;

  if (result == NULL)
    {
      return LC_ACCESSORY_RUNTIME_INVALID;
    }

  reset_result(result);
  if (runtime == NULL || !runtime->initialized || runtime->state == NULL)
    {
      result->status = LC_ACCESSORY_RUNTIME_INVALID;
      return result->status;
    }

  stream_status = lc_accessory_stream_feed(&runtime->stream, byte,
                                            &result->event);
  if (stream_status == LC_ACCESSORY_STREAM_NONE)
    {
      return result->status;
    }

  if (stream_status == LC_ACCESSORY_STREAM_DROPPED)
    {
      result->status = LC_ACCESSORY_RUNTIME_DROPPED;
      return result->status;
    }

  if (stream_status != LC_ACCESSORY_STREAM_EVENT)
    {
      result->status = LC_ACCESSORY_RUNTIME_INVALID;
      return result->status;
    }

  return lc_accessory_runtime_accept(runtime, &result->event, observed_ms,
                                     result);
}

lc_accessory_runtime_status_t lc_accessory_runtime_accept(
  lc_accessory_runtime_t *runtime,
  const lc_accessory_event_t *event,
  uint64_t observed_ms,
  lc_accessory_runtime_result_t *result)
{
  lc_presence_observation_t observation;
  lc_accessory_event_t accepted;

  if (result == NULL)
    {
      return LC_ACCESSORY_RUNTIME_INVALID;
    }
  if (event != NULL)
    {
      accepted = *event;
    }
  reset_result(result);
  if (runtime == NULL || !runtime->initialized || runtime->state == NULL ||
      event == NULL)
    {
      result->status = LC_ACCESSORY_RUNTIME_INVALID;
      return result->status;
    }

  result->event = accepted;
  result->acceptance = lc_accessory_cursor_accept(&runtime->cursor,
                                                   event);
  if (!lc_accessory_ack_format(&result->event, result->acceptance,
                               result->ack, sizeof(result->ack)))
    {
      result->status = LC_ACCESSORY_RUNTIME_INVALID;
      return result->status;
    }

  if (result->acceptance != LC_ACCESSORY_ACCEPTED &&
      result->acceptance != LC_ACCESSORY_NEW_SESSION)
    {
      result->status = LC_ACCESSORY_RUNTIME_REJECTED;
      return result->status;
    }

  result->status = LC_ACCESSORY_RUNTIME_ACCEPTED;
  if (result->event.type != LC_ACCESSORY_PRESENCE &&
      result->event.type != LC_ACCESSORY_PRESENCE_CONFIDENCE)
    {
      return result->status;
    }

  observation.source = runtime->source;
  observation.observed_ms = observed_ms;
  if (result->event.type == LC_ACCESSORY_PRESENCE)
    {
      observation.detected = result->event.value == 1;
      observation.confidence_percent = observation.detected ? 100u : 0u;
    }
  else
    {
      observation.detected = result->event.value > 0;
      observation.confidence_percent = (uint8_t)result->event.value;
    }

  result->actions = lc_presence_flow_handle(&runtime->presence,
                                             runtime->state, &observation);
  if ((result->actions & LC_ACTION_PLAY_GREETING) != 0u)
    {
      result->status = LC_ACCESSORY_RUNTIME_GREETING;
    }

  return result->status;
}

lc_accessory_runtime_status_t lc_accessory_runtime_accept_binary(
  lc_accessory_runtime_t *runtime,
  bool detected,
  uint64_t observed_ms,
  lc_accessory_runtime_result_t *result)
{
  lc_presence_observation_t observation;

  if (result == NULL)
    {
      return LC_ACCESSORY_RUNTIME_INVALID;
    }

  reset_result(result);
  if (runtime == NULL || !runtime->initialized || runtime->state == NULL ||
      !lc_presence_from_binary(runtime->source, detected, observed_ms,
                               &observation))
    {
      result->status = LC_ACCESSORY_RUNTIME_INVALID;
      return result->status;
    }

  result->event.type = LC_ACCESSORY_PRESENCE;
  result->event.value = detected ? 1 : 0;
  result->status = LC_ACCESSORY_RUNTIME_ACCEPTED;
  result->actions = lc_presence_flow_handle(&runtime->presence,
                                             runtime->state,
                                             &observation);
  if ((result->actions & LC_ACTION_PLAY_GREETING) != 0u)
    {
      result->status = LC_ACCESSORY_RUNTIME_GREETING;
    }

  return result->status;
}

lc_actions_t lc_accessory_runtime_finish_greeting(
  lc_accessory_runtime_t *runtime,
  uint64_t observed_ms)
{
  if (runtime == NULL || !runtime->initialized || runtime->state == NULL)
    {
      return LC_ACTION_NONE;
    }

  return lc_state_handle(runtime->state, LC_EVENT_GREETING_FINISHED,
                         observed_ms);
}
