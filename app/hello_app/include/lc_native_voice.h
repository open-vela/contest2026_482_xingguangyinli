#ifndef LC_NATIVE_VOICE_H
#define LC_NATIVE_VOICE_H
#include "lc_pcm_recorder.h"
#include "lc_pcm_player.h"
#include "lc_voice_client.h"

typedef struct lc_native_voice lc_native_voice_t;
typedef enum
{
  LC_NATIVE_VOICE_OK = 0, LC_NATIVE_VOICE_BUSY, LC_NATIVE_VOICE_INVALID,
  LC_NATIVE_VOICE_CANCELLED, LC_NATIVE_VOICE_CAPTURE_ERROR,
  LC_NATIVE_VOICE_SERVICE_ERROR, LC_NATIVE_VOICE_PLAY_ERROR
} lc_native_voice_status_t;

/* No recording/network on create. Takes recorder ownership on success only;
 * copies the configured client. NULL player means native media on NuttX.
 * Operation lock allows at most one recording/upload/playback owner. */
lc_native_voice_t *lc_native_voice_create(lc_pcm_recorder_t *recorder,
  const lc_voice_client_t *client, const lc_pcm_player_ops_t *player,
  void *player_context);
/* UI safe: recorder start is asynchronous; never waits for another operation.
 * NULL existing_session starts a new meal only when finish actually has audio.
 * Explicit existing session retains follow-up/preferences/refusal context. */
lc_native_voice_status_t lc_native_voice_start(lc_native_voice_t *voice,
  const char *existing_session, uint64_t *ticket);
/* WORKER ONLY. Stop/join capture, create/reuse session, upload, then wipe audio
 * on every outcome. Complete UTF8 or error, never truncated. */
lc_native_voice_status_t lc_native_voice_finish(lc_native_voice_t *voice,
  uint64_t ticket, char *session, size_t session_capacity,
  char *transcript, size_t transcript_capacity);
/* Nonblocking generation invalidation only: an active capture must still be
 * finished by its worker. Already sent provider calls cannot be retracted. */
void lc_native_voice_cancel(lc_native_voice_t *voice);
bool lc_native_voice_current(lc_native_voice_t *voice, uint64_t ticket);
/* WORKER ONLY: <=72 UTF8 chars, same explicit session; heap WAV, wipe/free after
 * playback, wait terminal completion. Never runs alongside capture. */
lc_native_voice_status_t lc_native_voice_speak(lc_native_voice_t *voice,
  const char *session, const char *text);
/* Worker only; no callers may use voice after destroy begins. */
void lc_native_voice_destroy(lc_native_voice_t *voice);
#ifdef __NuttX__
lc_native_voice_t *lc_native_voice_create_default(
  const lc_product_transport_t *backend, unsigned int max_ms);
#endif
#endif
