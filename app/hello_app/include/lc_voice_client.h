#ifndef LC_VOICE_CLIENT_H
#define LC_VOICE_CLIENT_H
#include "lc_product_transport.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define LC_VOICE_TOKEN_CONFIG_PATH LC_PRODUCT_DEVICE_TOKEN_CONFIG_PATH
#define LC_VOICE_CAPTURE_MAX 480044u
#define LC_VOICE_TRANSCRIPT_BYTES LC_PRODUCT_TRANSCRIPT_BYTES
#define LC_VOICE_SYNTHESIS_MAX 720044u /* 15s, 24k mono PCM16 plus WAV header. */

typedef enum
{
  LC_VOICE_CLIENT_OK = 0, LC_VOICE_CLIENT_INVALID,
  LC_VOICE_CLIENT_CANCELLED, LC_VOICE_CLIENT_NETWORK,
  LC_VOICE_CLIENT_HTTP, LC_VOICE_CLIENT_SCHEMA
} lc_voice_client_status_t;

/* Request is a single POST, binary-safe, never redirects/retries. Native
 * default verifies TLS peer/hostname using the existing product trust anchor.
 * Invoke only from a worker thread, never LVGL. */
typedef int (*lc_voice_request_fn)(void *context,
  const lc_product_transport_t *backend, const char *path, const char *mime,
  const char *authorization, const void *body, size_t body_length,
  char *response, size_t capacity, size_t *response_length);
typedef bool (*lc_voice_current_fn)(void *context, const char *session_id);
typedef struct
{
  lc_product_transport_t backend;
  char authorization[136];
  char device_id[64];
  lc_voice_request_fn request;
  void *context;
  bool configured;
} lc_voice_client_t;

/* Owner-device token only: NEVER pass a Qwen/provider key. No network on init. */
bool lc_voice_client_init(lc_voice_client_t *client,
  const lc_product_transport_t *backend, const char *device_token,
  const char *device_id, lc_voice_request_fn request, void *context);
/* Reads one token, with optional final LF/CRLF; rejects all trailing content.
 * Caller owns/closes stream. Only device token files, not provider secrets. */
bool lc_voice_client_init_stream(lc_voice_client_t *client,
  const lc_product_transport_t *backend, FILE *stream,
  const char *device_id, lc_voice_request_fn request, void *context);
void lc_voice_client_clear(lc_voice_client_t *client);
lc_voice_client_status_t lc_voice_client_open_session(lc_voice_client_t *client,
  char *session_id, size_t capacity);
/* Accepts canonical WAV from lc_pcm_recorder (16k mono PCM16 LE, <=15s).
 * Complete UTF8 or error, never silently truncates. current is required and
 * checked before/after request. Owner must check generation again before UI
 * publication; cancellation cannot retract a provider call already sent. */
lc_voice_client_status_t lc_voice_client_transcribe(lc_voice_client_t *client,
  const char *session_id, const uint8_t *wav, size_t length,
  char *transcript, size_t capacity, lc_voice_current_fn current, void *context);
/* Worker only. text is complete UTF8, <=72 characters; no splitting/retries.
 * Caller owns an in-memory buffer of at least LC_VOICE_SYNTHESIS_MAX + 2 bytes
 * (TLS collector reserves a terminator and truncation sentinel). Returns a
 * canonical 24k mono PCM16 WAV, never plays it. Failure/cancellation clears
 * length and wipes the bounded buffer. Recheck generation before playback. */
lc_voice_client_status_t lc_voice_client_synthesize(lc_voice_client_t *client,
  const char *session_id, const char *text, uint8_t *wav, size_t capacity,
  size_t *length, lc_voice_current_fn current, void *context);
#ifdef __NuttX__
bool lc_voice_client_init_default(lc_voice_client_t *client,
  const lc_product_transport_t *backend);
#endif
#endif
