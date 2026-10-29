#define _POSIX_C_SOURCE 200809L
#include "lc_pcm_recorder.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __NuttX__
#include <voice/audio_capture.h>
#endif

#define PCM_RATE 16000u
#define PCM_FRAME_BYTES 2u
#define WAV_HEADER_BYTES 44u
#define READ_CHUNK_BYTES 1024u
#define WORKER_STACK_BYTES 16384u

struct lc_pcm_recorder
{
  lc_pcm_recorder_ops_t ops;
  pthread_mutex_t lock;
  pthread_t reader;
  pthread_t watchdog;
  void *handle;
  uint8_t *buffer;
  size_t capacity;
  size_t used;
  uint64_t started_ms;
  unsigned int max_ms;
  lc_pcm_status_t result;
  bool active;
  bool stopped;
  bool done;
};

static void wipe(uint8_t *bytes, size_t count)
{
  volatile uint8_t *destination = bytes;
  while (count-- != 0u) *destination++ = 0u;
}

static uint64_t monotonic_ms(void)
{
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return UINT64_MAX;
  return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void stop_locked(lc_pcm_recorder_t *recorder)
{
  if (!recorder->stopped)
    {
      recorder->stopped = true;
      if (recorder->handle != NULL)
        (void)recorder->ops.abort(recorder->handle);
    }
}

static void *watchdog_worker(void *argument)
{
  lc_pcm_recorder_t *recorder = argument;
  const struct timespec delay = {.tv_sec = 0, .tv_nsec = 10000000};
  for (;;)
    {
      pthread_mutex_lock(&recorder->lock);
      if (recorder->done || recorder->stopped)
        {
          pthread_mutex_unlock(&recorder->lock);
          return NULL;
        }
      if (monotonic_ms() - recorder->started_ms >= recorder->max_ms)
        {
          stop_locked(recorder);
          pthread_mutex_unlock(&recorder->lock);
          return NULL;
        }
      pthread_mutex_unlock(&recorder->lock);
      nanosleep(&delay, NULL);
    }
}

static void *reader_worker(void *argument)
{
  lc_pcm_recorder_t *recorder = argument;
  uint8_t chunk[READ_CHUNK_BYTES];
  void *handle;
  bool stopped;
  pthread_mutex_lock(&recorder->lock);
  stopped = recorder->stopped;
  pthread_mutex_unlock(&recorder->lock);
  handle = stopped ? NULL : recorder->ops.open();
  if (handle == NULL)
    {
      pthread_mutex_lock(&recorder->lock);
      if (!stopped) recorder->result = LC_PCM_DEVICE_ERROR;
      recorder->done = true;
      pthread_mutex_unlock(&recorder->lock);
      return NULL;
    }

  /* Publish before reading so watchdog/finish can interrupt blocking read.
   * Do not race abort with SDK initialization/start. */
  int start_result = recorder->ops.start(handle);
  pthread_mutex_lock(&recorder->lock);
  recorder->handle = handle;
  if (start_result != 0) recorder->result = LC_PCM_DEVICE_ERROR;
  if (recorder->stopped || start_result != 0)
    (void)recorder->ops.abort(handle);
  pthread_mutex_unlock(&recorder->lock);

  for (;;)
    {
      pthread_mutex_lock(&recorder->lock);
      stopped = recorder->stopped || recorder->result != LC_PCM_OK;
      pthread_mutex_unlock(&recorder->lock);
      if (stopped) break;
      size_t remaining = recorder->capacity - WAV_HEADER_BYTES - recorder->used;
      if (remaining == 0u) break;
      size_t requested = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
      int count = recorder->ops.read(handle, chunk, requested);
      pthread_mutex_lock(&recorder->lock);
      if (!recorder->stopped)
        {
          if (count < 0 || (size_t)count > requested || count % 2 != 0)
            recorder->result = LC_PCM_DEVICE_ERROR;
          else if (count > 0)
            {
              memcpy(recorder->buffer + WAV_HEADER_BYTES + recorder->used,
                     chunk, (size_t)count);
              recorder->used += (size_t)count;
            }
        }
      stopped = recorder->stopped || recorder->result != LC_PCM_OK || count == 0;
      pthread_mutex_unlock(&recorder->lock);
      if (stopped) break;
    }
  wipe(chunk, sizeof(chunk));
  pthread_mutex_lock(&recorder->lock);
  stop_locked(recorder);
  recorder->ops.close(handle);
  recorder->handle = NULL;
  recorder->done = true;
  pthread_mutex_unlock(&recorder->lock);
  return NULL;
}

lc_pcm_recorder_t *lc_pcm_recorder_create(const lc_pcm_recorder_ops_t *ops,
                                         unsigned int max_ms)
{
  if (ops == NULL || ops->open == NULL || ops->start == NULL ||
      ops->read == NULL || ops->abort == NULL || ops->close == NULL ||
      max_ms == 0u || max_ms > 15000u) return NULL;
  lc_pcm_recorder_t *recorder = calloc(1u, sizeof(*recorder));
  if (recorder == NULL) return NULL;
  recorder->capacity = WAV_HEADER_BYTES +
    (size_t)PCM_RATE * PCM_FRAME_BYTES * max_ms / 1000u;
  recorder->buffer = calloc(1u, recorder->capacity);
  if (recorder->buffer == NULL || pthread_mutex_init(&recorder->lock, NULL) != 0)
    {
      free(recorder->buffer);
      free(recorder);
      return NULL;
    }
  recorder->ops = *ops;
  recorder->max_ms = max_ms;
  return recorder;
}

lc_pcm_status_t lc_pcm_recorder_start(lc_pcm_recorder_t *recorder)
{
  pthread_attr_t attributes;
  if (recorder == NULL) return LC_PCM_INVALID;
  pthread_mutex_lock(&recorder->lock);
  if (recorder->active)
    {
      pthread_mutex_unlock(&recorder->lock);
      return LC_PCM_BUSY;
    }
  recorder->started_ms = monotonic_ms();
  if (recorder->started_ms == UINT64_MAX)
    {
      pthread_mutex_unlock(&recorder->lock);
      return LC_PCM_THREAD_ERROR;
    }
  recorder->active = true;
  recorder->done = recorder->stopped = false;
  recorder->result = LC_PCM_OK;
  recorder->used = 0u;
  wipe(recorder->buffer, recorder->capacity);
  pthread_mutex_unlock(&recorder->lock);
  if (pthread_attr_init(&attributes) != 0)
    {
      recorder->active = false;
      return LC_PCM_THREAD_ERROR;
    }
  if (pthread_attr_setstacksize(&attributes, WORKER_STACK_BYTES) != 0)
    {
      pthread_attr_destroy(&attributes);
      recorder->active = false;
      return LC_PCM_THREAD_ERROR;
    }
  if (pthread_create(&recorder->watchdog, &attributes, watchdog_worker, recorder) != 0)
    {
      pthread_attr_destroy(&attributes);
      recorder->active = false;
      return LC_PCM_THREAD_ERROR;
    }
  if (pthread_create(&recorder->reader, &attributes, reader_worker, recorder) != 0)
    {
      pthread_attr_destroy(&attributes);
      pthread_mutex_lock(&recorder->lock);
      recorder->stopped = true;
      pthread_mutex_unlock(&recorder->lock);
      pthread_join(recorder->watchdog, NULL);
      recorder->active = false;
      return LC_PCM_THREAD_ERROR;
    }
  pthread_attr_destroy(&attributes);
  return LC_PCM_OK;
}

static void put16(uint8_t *bytes, uint16_t value)
{
  bytes[0] = (uint8_t)value;
  bytes[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *bytes, uint32_t value)
{
  put16(bytes, (uint16_t)value);
  put16(bytes + 2, (uint16_t)(value >> 16));
}

lc_pcm_status_t lc_pcm_recorder_finish(lc_pcm_recorder_t *recorder, bool cancel,
                                      const uint8_t **wav, size_t *length)
{
  if (wav == NULL || length == NULL) return LC_PCM_INVALID;
  *wav = NULL;
  *length = 0u;
  if (recorder == NULL) return LC_PCM_INVALID;
  if (!recorder->active) return LC_PCM_IDLE;
  pthread_mutex_lock(&recorder->lock);
  stop_locked(recorder);
  pthread_mutex_unlock(&recorder->lock);
  pthread_join(recorder->reader, NULL);
  pthread_join(recorder->watchdog, NULL);
  recorder->active = false;
  lc_pcm_status_t status = cancel ? LC_PCM_CANCELLED : recorder->result;
  if (status == LC_PCM_OK && recorder->used == 0u) status = LC_PCM_EMPTY;
  if (status != LC_PCM_OK)
    {
      wipe(recorder->buffer, recorder->capacity);
      recorder->used = 0u;
      return status;
    }
  uint8_t *bytes = recorder->buffer;
  memcpy(bytes, "RIFF", 4u);
  put32(bytes + 4, (uint32_t)recorder->used + 36u);
  memcpy(bytes + 8, "WAVEfmt ", 8u);
  put32(bytes + 16, 16u);
  put16(bytes + 20, 1u);
  put16(bytes + 22, 1u);
  put32(bytes + 24, PCM_RATE);
  put32(bytes + 28, PCM_RATE * PCM_FRAME_BYTES);
  put16(bytes + 32, PCM_FRAME_BYTES);
  put16(bytes + 34, 16u);
  memcpy(bytes + 36, "data", 4u);
  put32(bytes + 40, (uint32_t)recorder->used);
  *wav = bytes;
  *length = WAV_HEADER_BYTES + recorder->used;
  return LC_PCM_OK;
}

lc_pcm_status_t lc_pcm_recorder_discard(lc_pcm_recorder_t *recorder)
{
  if (recorder == NULL) return LC_PCM_INVALID;
  if (recorder->active) return LC_PCM_BUSY;
  wipe(recorder->buffer, recorder->capacity);
  recorder->used = 0u;
  return LC_PCM_OK;
}

void lc_pcm_recorder_destroy(lc_pcm_recorder_t *recorder)
{
  if (recorder == NULL) return;
  const uint8_t *wav;
  size_t length;
  if (recorder->active)
    (void)lc_pcm_recorder_finish(recorder, true, &wav, &length);
  wipe(recorder->buffer, recorder->capacity);
  free(recorder->buffer);
  pthread_mutex_destroy(&recorder->lock);
  free(recorder);
}

#ifdef __NuttX__
static void *native_open(void)
{
  return audio_capture_open("/dev/audio/pcm0c", PCM_RATE, 1u, 16u);
}
static int native_start(void *handle) { return audio_capture_start(handle); }
static int native_read(void *handle, void *buffer, size_t capacity)
{
  return audio_capture_read(handle, buffer, capacity);
}
static int native_abort(void *handle) { return audio_capture_abort(handle); }
static void native_close(void *handle) { audio_capture_close(handle); }
lc_pcm_recorder_t *lc_pcm_recorder_create_native(unsigned int max_ms)
{
  const lc_pcm_recorder_ops_t ops = {
    .open = native_open, .start = native_start, .read = native_read,
    .abort = native_abort, .close = native_close
  };
  return lc_pcm_recorder_create(&ops, max_ms);
}
#endif
