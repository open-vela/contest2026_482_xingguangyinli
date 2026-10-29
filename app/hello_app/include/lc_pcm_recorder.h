#ifndef LC_PCM_RECORDER_H
#define LC_PCM_RECORDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct lc_pcm_recorder lc_pcm_recorder_t;
typedef enum
{
  LC_PCM_OK = 0, LC_PCM_BUSY, LC_PCM_IDLE, LC_PCM_EMPTY,
  LC_PCM_CANCELLED, LC_PCM_DEVICE_ERROR, LC_PCM_THREAD_ERROR, LC_PCM_INVALID
} lc_pcm_status_t;

/* Hardware boundary: read may block, abort must interrupt it without freeing
 * the handle. The recorder serializes abort/close. PCM is 16kHz mono PCM16 LE.
 * Only one SDK capture owner may exist (never run the old voice channel too). */
typedef struct
{
  void *(*open)(void);
  int (*start)(void *handle);
  int (*read)(void *handle, void *buffer, size_t capacity);
  int (*abort)(void *handle);
  void (*close)(void *handle);
} lc_pcm_recorder_ops_t;

/* create does NOT open a microphone. max_ms is 1..15000; both time and bytes
 * are limited. start launches workers and never opens the device on the caller
 * thread. Caller lifecycle methods must be serialized by the owning adapter. */
lc_pcm_recorder_t *lc_pcm_recorder_create(const lc_pcm_recorder_ops_t *ops,
                                         unsigned int max_ms);
lc_pcm_status_t lc_pcm_recorder_start(lc_pcm_recorder_t *recorder);
/* WORKER THREAD ONLY: requests abort, joins workers, returns borrowed WAV
 * until next start/destroy. cancel=true discards all audio. Never call from
 * LVGL. SDK abort/open/start/close reliability still needs physical validation;
 * the watchdog requests interruption, not a hard kill of a defective driver. */
lc_pcm_status_t lc_pcm_recorder_finish(lc_pcm_recorder_t *recorder, bool cancel,
                                      const uint8_t **wav, size_t *length);
/* Worker only, serialized lifecycle: wipe borrowed audio after upload, without
 * opening another microphone or releasing the reusable recorder. Busy during
 * capture; all borrowed WAV pointers become cleared on success. */
lc_pcm_status_t lc_pcm_recorder_discard(lc_pcm_recorder_t *recorder);
/* WORKER THREAD ONLY: cancels any active capture and wipes/releases memory. */
void lc_pcm_recorder_destroy(lc_pcm_recorder_t *recorder);

#ifdef __NuttX__
lc_pcm_recorder_t *lc_pcm_recorder_create_native(unsigned int max_ms);
#endif
#endif
