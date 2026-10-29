#ifndef LC_PCM_PLAYER_H
#define LC_PCM_PLAYER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
  LC_PCM_PLAYER_OK = 0, LC_PCM_PLAYER_INVALID, LC_PCM_PLAYER_CANCELLED,
  LC_PCM_PLAYER_TIMEOUT, LC_PCM_PLAYER_DEVICE_ERROR, LC_PCM_PLAYER_UNAVAILABLE
} lc_pcm_player_status_t;
typedef void (*lc_pcm_player_notify_fn)(void *cookie, bool failed);
typedef bool (*lc_pcm_player_current_fn)(void *context);

/* Only hardware boundary is injected. notify means terminal completion/error,
 * not merely accepted bytes. write returns partial bytes or 0 for backpressure;
 * it must not block on a full queue. end signals EOF without stopping output.
 * Successful close MUST quiesce callbacks; failed close leaves cookie alive. */
typedef struct
{
  void *(*open)(void *context, lc_pcm_player_notify_fn notify, void *cookie);
  int (*write)(void *context, void *handle, const uint8_t *bytes, size_t length);
  int (*end)(void *context, void *handle);
  void (*stop)(void *context, void *handle);
  int (*close)(void *context, void *handle);
} lc_pcm_player_ops_t;

/* WORKER ONLY, synchronous, serialized; PCM payload WITHOUT a WAV header,
 * 24k mono PCM16 LE, 1..720000 bytes, even length. Borrowed until return.
 * timeout_ms is 1..20000. current required, checked throughout. Never logs,
 * persists or owns PCM. On SDK close failure retains one bounded callback
 * context and permanently disables this adapter to avoid use-after-free.
 * SDK startup/socket accept/stop/close may block beyond the polling deadline;
 * physical cancellation/drain remains an acceptance gate. Do not overlap
 * the legacy voice_channel or another application's audio owner. */
lc_pcm_player_status_t lc_pcm_player_play(const lc_pcm_player_ops_t *ops,
  void *device_context, const uint8_t *pcm, size_t length,
  lc_pcm_player_current_fn current, void *context, unsigned int timeout_ms);
#ifdef __NuttX__
lc_pcm_player_status_t lc_pcm_player_play_native(const uint8_t *pcm, size_t length,
  lc_pcm_player_current_fn current, void *context);
#endif
#endif
