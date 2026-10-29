#define _POSIX_C_SOURCE 200809L
#include "lc_native_voice.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char sid[] = "meal_0123456789ab";
static const char phrase[] = "一个人想吃微辣的饭，三十元，半小时内吃到";
static lc_native_voice_t *voice;
static int calls, sessions, uploads, speech, opens, read_count;
static bool cancel_request, cancel_session, empty_capture;
static int upload_status = 200;
static const uint8_t *borrowed_audio;
static size_t borrowed_length;
static lc_pcm_player_notify_fn notify;
static void *cookie;
static uint8_t reply[46] = {
 'R','I','F','F',38,0,0,0,'W','A','V','E','f','m','t',' ',
 16,0,0,0,1,0,1,0,0xc0,0x5d,0,0,0x80,0xbb,0,0,2,0,16,0,
 'd','a','t','a',2,0,0,0,0x21,0
};
static void *capture_open(void) { opens++; read_count = 0; return &opens; }
static int capture_start(void *h) { assert(h == &opens); return 0; }
static int capture_read(void *h, void *b, size_t n)
{
 assert(h == &opens);
 if (empty_capture) return 0;
 if (read_count++ != 0) return 0;
 memset(b, 0x21, n); return (int)n;
}
static int capture_abort(void *h) { assert(h == &opens); return 0; }
static void capture_close(void *h) { assert(h == &opens); }
static void *player_open(void *ctx, lc_pcm_player_notify_fn fn, void *c)
{ (void)ctx; notify = fn; cookie = c; return &speech; }
static int player_write(void *ctx, void *h, const uint8_t *b, size_t n)
{ (void)ctx; assert(h == &speech && n == 2 && b[0] == 0x21); speech++; return (int)n; }
static int player_end(void *ctx, void *h)
{ (void)ctx; assert(h == &speech); notify(cookie, false); return 0; }
static void player_stop(void *ctx, void *h) { (void)ctx; (void)h; }
static int player_close(void *ctx, void *h) { (void)ctx; (void)h; return 0; }
static int request(void *ctx, const lc_product_transport_t *backend,
 const char *path, const char *mime, const char *auth, const void *body,
 size_t size, char *response, size_t capacity, size_t *received)
{
 (void)ctx; (void)backend; (void)auth; calls++;
 const char *json;
 if (strcmp(path, "/api/v1/session") == 0)
 {
   sessions++; json = "{\"session_id\":\"meal_0123456789ab\"}";
   if (cancel_session) lc_native_voice_cancel(voice);
 }
 else if (strcmp(path, "/api/v1/voice/asr?session_id=meal_0123456789ab") == 0)
 {
   assert(strcmp(mime, "audio/wav") == 0 && size > 44);
   assert(memcmp(body, "RIFF", 4) == 0);
   borrowed_audio = body; borrowed_length = size; uploads++;
   uint64_t rejected_ticket;
   assert(lc_native_voice_start(voice, NULL, &rejected_ticket) == LC_NATIVE_VOICE_BUSY);
   assert(lc_native_voice_speak(voice, sid, "想试试鸡腿饭吗？") == LC_NATIVE_VOICE_BUSY);
   if (cancel_request) lc_native_voice_cancel(voice);
   char json_reply[512];
   int n = snprintf(json_reply, sizeof(json_reply),
    "{\"session_id\":\"%s\",\"transcript\":\"%s\"}", sid, phrase);
   assert(n > 0 && (size_t)n < capacity);
   memcpy(response, json_reply, (size_t)n); *received = (size_t)n; return upload_status;
 }
 else
 {
   assert(strcmp(path, "/api/v1/voice/tts?session_id=meal_0123456789ab") == 0);
   assert(strcmp(mime, "text/plain; charset=utf-8") == 0);
   assert(size == strlen("想试试鸡腿饭吗？"));
   if (cancel_request) lc_native_voice_cancel(voice);
   assert(sizeof(reply) < capacity);
   memcpy(response, reply, sizeof(reply)); *received = sizeof(reply); return 200;
 }
 *received = strlen(json); assert(*received < capacity);
 memcpy(response, json, *received); return 201;
}
static void wait_capture(void)
{ struct timespec t = {.tv_sec = 0, .tv_nsec = 30000000}; nanosleep(&t, NULL); }
int main(void)
{
 lc_product_transport_t backend; lc_voice_client_t client;
 lc_pcm_recorder_ops_t capture = {capture_open, capture_start, capture_read,
                                capture_abort, capture_close};
 lc_pcm_player_ops_t player = {player_open, player_write, player_end,
                              player_stop, player_close};
 assert(lc_product_transport_init(&backend, "https://example.test/api"));
 assert(lc_voice_client_init(&client, &backend,
  "lc-owner-test-device-01234567890123456789", "gemini-s1", request, NULL));
 voice = lc_native_voice_create(lc_pcm_recorder_create(&capture, 50),
                                &client, &player, NULL);
 lc_voice_client_clear(&client); /* Adapter owns its own credential copy. */
 assert(voice != NULL && opens == 0 && calls == 0);
 uint64_t ticket = 0, rejected;
 char session[LC_PRODUCT_SESSION_MAX], text[LC_VOICE_TRANSCRIPT_BYTES];
 assert(lc_native_voice_start(voice, NULL, &ticket) == LC_NATIVE_VOICE_OK);
 assert(lc_native_voice_start(voice, NULL, &rejected) == LC_NATIVE_VOICE_BUSY);
 assert(lc_native_voice_speak(voice, sid, "想试试鸡腿饭吗？") == LC_NATIVE_VOICE_BUSY);
 wait_capture();
 assert(lc_native_voice_finish(voice, ticket, session, sizeof(session),
  text, sizeof(text)) == LC_NATIVE_VOICE_OK);
 assert(strcmp(session, sid) == 0 && strcmp(text, phrase) == 0);
 assert(sessions == 1 && uploads == 1 && lc_native_voice_current(voice, ticket));
 for (size_t i = 0; i < borrowed_length; i++) assert(borrowed_audio[i] == 0);
 assert(lc_native_voice_speak(voice, sid, "想试试鸡腿饭吗？") == LC_NATIVE_VOICE_OK);
 assert(speech == 1); /* Actual PCM player received payload, no WAV header. */
 assert(!lc_native_voice_current(voice, ticket));

 assert(lc_native_voice_start(voice, sid, &ticket) == LC_NATIVE_VOICE_OK);
 wait_capture();
 assert(lc_native_voice_finish(voice, ticket, session, sizeof(session),
  text, sizeof(text)) == LC_NATIVE_VOICE_OK);
 assert(sessions == 1 && uploads == 2); /* Same-session follow-up, not a new user. */
 cancel_request = true;
 assert(lc_native_voice_start(voice, sid, &ticket) == LC_NATIVE_VOICE_OK);
 wait_capture();
 assert(lc_native_voice_finish(voice, ticket, session, sizeof(session),
  text, sizeof(text)) == LC_NATIVE_VOICE_CANCELLED);
 assert(text[0] == 0 && session[0] == 0 && !lc_native_voice_current(voice, ticket));
 for (size_t i = 0; i < borrowed_length; i++) assert(borrowed_audio[i] == 0);
 assert(lc_native_voice_speak(voice, sid, "想试试鸡腿饭吗？") == LC_NATIVE_VOICE_CANCELLED);
 assert(speech == 1); /* Cancelled TTS never reaches speaker. */
 cancel_request = false;
 int before = calls;
 assert(lc_native_voice_start(voice, sid, &ticket) == LC_NATIVE_VOICE_OK);
 wait_capture(); lc_native_voice_cancel(voice);
 assert(lc_native_voice_finish(voice, ticket, session, sizeof(session),
  text, sizeof(text)) == LC_NATIVE_VOICE_CANCELLED);
 assert(calls == before && text[0] == 0 && session[0] == 0);
 upload_status = 503;
 assert(lc_native_voice_start(voice, sid, &ticket) == LC_NATIVE_VOICE_OK);
 wait_capture(); before = calls;
 assert(lc_native_voice_finish(voice, ticket, session, sizeof(session),
  text, sizeof(text)) == LC_NATIVE_VOICE_SERVICE_ERROR);
 assert(calls == before + 1 && text[0] == 0 && session[0] == 0);
 for (size_t i = 0; i < borrowed_length; i++) assert(borrowed_audio[i] == 0);
 upload_status = 200;
 cancel_session = true;
 assert(lc_native_voice_start(voice, NULL, &ticket) == LC_NATIVE_VOICE_OK);
 wait_capture(); before = calls;
 assert(lc_native_voice_finish(voice, ticket, session, sizeof(session),
  text, sizeof(text)) == LC_NATIVE_VOICE_CANCELLED);
 assert(calls == before + 1); /* Do not upload after cancelled session creation. */
 cancel_session = false; empty_capture = true;
 assert(lc_native_voice_start(voice, NULL, &ticket) == LC_NATIVE_VOICE_OK);
 wait_capture(); before = calls;
 assert(lc_native_voice_finish(voice, ticket, session, sizeof(session),
  text, sizeof(text)) == LC_NATIVE_VOICE_CAPTURE_ERROR);
 assert(calls == before); /* Silence/no samples doesn't manufacture speech. */
 assert(lc_native_voice_start(voice, "meal_bad", &ticket) == LC_NATIVE_VOICE_INVALID);
 lc_native_voice_destroy(voice);
 puts("PASS: real recorder/client/player composition, same session, generation cancellation, post-upload wipe");
 return 0;
}
