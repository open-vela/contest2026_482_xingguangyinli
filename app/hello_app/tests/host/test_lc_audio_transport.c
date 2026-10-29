#include "lc_audio_transport.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int g_start_result;
static int g_stop_result;
static int g_speak_result;
static unsigned int g_start_calls;
static unsigned int g_stop_calls;
static unsigned int g_speak_calls;
static const char *g_transcript;
static char g_spoken[128];

static int fake_start(void)
{
  g_start_calls++;
  return g_start_result;
}

static int fake_stop_with_text(char *text, size_t capacity)
{
  size_t length;

  g_stop_calls++;
  if (g_stop_result != 0)
    {
      return g_stop_result;
    }

  length = strlen(g_transcript);
  if (length + 1u > capacity)
    {
      return -1;
    }

  memcpy(text, g_transcript, length + 1u);
  return 0;
}

static int fake_speak(const char *text)
{
  g_speak_calls++;
  (void)snprintf(g_spoken, sizeof(g_spoken), "%s", text);
  return g_speak_result;
}

static void reset_fakes(void)
{
  g_start_result = 0;
  g_stop_result = 0;
  g_speak_result = 0;
  g_start_calls = 0u;
  g_stop_calls = 0u;
  g_speak_calls = 0u;
  g_transcript = "今天想吃清淡一点";
  g_spoken[0] = '\0';
}

static lc_audio_transport_t make_transport(void)
{
  lc_audio_transport_t transport;
  const lc_audio_transport_ops_t ops =
  {
    .start = fake_start,
    .stop_with_text = fake_stop_with_text,
    .speak = fake_speak
  };

  assert(lc_audio_transport_init(&transport, &ops));
  return transport;
}

static void test_requires_explicit_start_before_audio_is_sent(void)
{
  lc_audio_transport_t transport = make_transport();
  char text[64];

  assert(lc_audio_transport_stop(&transport, text, sizeof(text)) ==
         LC_AUDIO_NOT_RECORDING);
  assert(g_stop_calls == 0u);
  assert(lc_audio_transport_is_recording(&transport) == false);
}

static void test_start_and_stop_return_a_bounded_transcript(void)
{
  lc_audio_transport_t transport = make_transport();
  char text[64];

  assert(lc_audio_transport_start(&transport) == LC_AUDIO_OK);
  assert(lc_audio_transport_is_recording(&transport));
  assert(g_start_calls == 1u);
  assert(lc_audio_transport_start(&transport) == LC_AUDIO_BUSY);
  assert(g_start_calls == 1u);

  assert(lc_audio_transport_stop(&transport, text, sizeof(text)) ==
         LC_AUDIO_OK);
  assert(strcmp(text, g_transcript) == 0);
  assert(!lc_audio_transport_is_recording(&transport));
  assert(g_stop_calls == 1u);
}

static void test_empty_transcript_is_reported_without_sticking_recording(void)
{
  lc_audio_transport_t transport = make_transport();
  char text[32];

  g_transcript = "";
  assert(lc_audio_transport_start(&transport) == LC_AUDIO_OK);
  assert(lc_audio_transport_stop(&transport, text, sizeof(text)) ==
         LC_AUDIO_EMPTY);
  assert(text[0] == '\0');
  assert(!lc_audio_transport_is_recording(&transport));
}

static void test_backend_failures_are_recoverable(void)
{
  lc_audio_transport_t transport = make_transport();
  char text[32];

  g_start_result = -5;
  assert(lc_audio_transport_start(&transport) == LC_AUDIO_BACKEND_ERROR);
  assert(!lc_audio_transport_is_recording(&transport));

  g_start_result = 0;
  assert(lc_audio_transport_start(&transport) == LC_AUDIO_OK);
  g_stop_result = -5;
  assert(lc_audio_transport_stop(&transport, text, sizeof(text)) ==
         LC_AUDIO_BACKEND_ERROR);
  assert(!lc_audio_transport_is_recording(&transport));
}

static void test_speech_rejects_empty_text_and_serializes_valid_text(void)
{
  lc_audio_transport_t transport = make_transport();

  assert(lc_audio_transport_speak(&transport, "") == LC_AUDIO_INVALID);
  assert(g_speak_calls == 0u);
  assert(lc_audio_transport_speak(&transport, "晚餐建议已经准备好") ==
         LC_AUDIO_OK);
  assert(g_speak_calls == 1u);
  assert(strcmp(g_spoken, "晚餐建议已经准备好") == 0);
}

int main(void)
{
  reset_fakes();
  test_requires_explicit_start_before_audio_is_sent();
  reset_fakes();
  test_start_and_stop_return_a_bounded_transcript();
  reset_fakes();
  test_empty_transcript_is_reported_without_sticking_recording();
  reset_fakes();
  test_backend_failures_are_recoverable();
  reset_fakes();
  test_speech_rejects_empty_text_and_serializes_valid_text();
  puts("lc_audio_transport tests passed");
  return 0;
}
