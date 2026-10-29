#define _POSIX_C_SOURCE 200809L
#include "lc_pcm_recorder.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static struct
{
  pthread_mutex_t lock;
  pthread_cond_t condition;
  int aborts;
  int closes;
  int reads;
  int opens;
  int mode;
  bool stopped;
} device = { .lock = PTHREAD_MUTEX_INITIALIZER,
             .condition = PTHREAD_COND_INITIALIZER };

static uint64_t now_ms(void)
{
  struct timespec now;
  assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
  return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static void reset(int mode)
{
  device.aborts = device.closes = device.reads = device.opens = 0;
  device.stopped = false;
  device.mode = mode;
}

static void *open_device(void)
{
  device.opens++;
  return device.mode == 1 ? NULL : &device;
}

static int start_device(void *handle)
{
  assert(handle == &device);
  return device.mode == 2 ? -EIO : 0;
}

static int read_device(void *handle, void *buffer, size_t capacity)
{
  assert(handle == &device);
  pthread_mutex_lock(&device.lock);
  device.reads++;
  if (device.mode == 3)
    {
      while (!device.stopped)
        pthread_cond_wait(&device.condition, &device.lock);
      pthread_mutex_unlock(&device.lock);
      return -ECANCELED;
    }
  if (device.mode == 4)
    {
      pthread_mutex_unlock(&device.lock);
      return -EIO;
    }
  if (device.mode == 5)
    {
      pthread_mutex_unlock(&device.lock);
      return 1; /* Invalid partial PCM16 sample. */
    }
  if (device.mode == 6 && device.reads > 1)
    {
      pthread_mutex_unlock(&device.lock);
      return -EIO;
    }
  bool stopped = device.stopped;
  pthread_mutex_unlock(&device.lock);
  if (stopped) return -ECANCELED;
  memset(buffer, 0x21, capacity);
  struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000};
  nanosleep(&delay, NULL);
  return (int)capacity;
}

static int abort_device(void *handle)
{
  assert(handle == &device);
  pthread_mutex_lock(&device.lock);
  device.aborts++;
  device.stopped = true;
  pthread_cond_broadcast(&device.condition);
  pthread_mutex_unlock(&device.lock);
  return 0;
}

static void close_device(void *handle)
{
  assert(handle == &device);
  device.closes++;
}

static const lc_pcm_recorder_ops_t ops = {
  .open = open_device, .start = start_device, .read = read_device,
  .abort = abort_device, .close = close_device
};

static uint32_t little32(const uint8_t *bytes)
{
  return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
    (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

int main(void)
{
  const uint8_t *wav = NULL;
  size_t length = 0;
  uint64_t started;
  lc_pcm_recorder_t *recorder;
  assert(lc_pcm_recorder_create(NULL, 50) == NULL);
  assert(lc_pcm_recorder_create(&ops, 0) == NULL);
  assert(lc_pcm_recorder_create(&ops, 15001) == NULL);
  reset(0);
  recorder = lc_pcm_recorder_create(&ops, 50);
  assert(recorder != NULL && device.opens == 0); /* No implicit listening. */
  assert(lc_pcm_recorder_finish(recorder, false, &wav, &length) == LC_PCM_IDLE);
  assert(wav == NULL && length == 0);
  assert(lc_pcm_recorder_start(recorder) == LC_PCM_OK);
  assert(lc_pcm_recorder_start(recorder) == LC_PCM_BUSY);
  assert(lc_pcm_recorder_discard(recorder) == LC_PCM_BUSY);
  /* Wait beyond the byte/time limit without explicitly stopping. */
  struct timespec delay = {.tv_sec = 0, .tv_nsec = 90000000};
  nanosleep(&delay, NULL);
  assert(lc_pcm_recorder_finish(recorder, false, &wav, &length) == LC_PCM_OK);
  assert(length == 44u + 16000u * 2u * 50u / 1000u);
  assert(memcmp(wav, "RIFF", 4) == 0 && memcmp(wav + 8, "WAVEfmt ", 8) == 0);
  assert(wav[20] == 1 && wav[22] == 1 && wav[34] == 16);
  assert(little32(wav + 24) == 16000 && little32(wav + 28) == 32000);
  assert(little32(wav + 40) == length - 44 && little32(wav + 4) == length - 8);
  assert(wav[44] == 0x21 && device.closes == 1 && device.aborts >= 1);
  assert(lc_pcm_recorder_discard(recorder) == LC_PCM_OK);
  for (size_t i = 0u; i < length; ++i) assert(wav[i] == 0u);
  assert(lc_pcm_recorder_discard(NULL) == LC_PCM_INVALID);
  lc_pcm_recorder_destroy(recorder);

  reset(3); /* A blocking hardware read must be interrupted by watchdog. */
  recorder = lc_pcm_recorder_create(&ops, 40);
  started = now_ms();
  assert(lc_pcm_recorder_start(recorder) == LC_PCM_OK);
  delay.tv_nsec = 80000000;
  nanosleep(&delay, NULL);
  assert(lc_pcm_recorder_finish(recorder, false, &wav, &length) == LC_PCM_EMPTY);
  assert(now_ms() - started < 500 && wav == NULL && length == 0);
  assert(device.aborts >= 1 && device.closes == 1);
  lc_pcm_recorder_destroy(recorder);

  reset(0); /* Early user stop returns only actually recorded complete samples. */
  recorder = lc_pcm_recorder_create(&ops, 1000);
  assert(lc_pcm_recorder_start(recorder) == LC_PCM_OK);
  delay.tv_nsec = 4000000;
  nanosleep(&delay, NULL);
  assert(lc_pcm_recorder_finish(recorder, false, &wav, &length) == LC_PCM_OK);
  assert(length > 44 && length < 32044 && (length - 44) % 2u == 0u);
  lc_pcm_recorder_destroy(recorder);

  reset(0); /* Cancel after collecting audio must discard the entire buffer. */
  recorder = lc_pcm_recorder_create(&ops, 1000);
  assert(lc_pcm_recorder_start(recorder) == LC_PCM_OK);
  nanosleep(&delay, NULL);
  assert(lc_pcm_recorder_finish(recorder, true, &wav, &length) == LC_PCM_CANCELLED);
  assert(wav == NULL && length == 0 && device.closes == 1);
  /* A cancelled instance can be explicitly restarted, never on its own. */
  reset(0);
  assert(lc_pcm_recorder_start(recorder) == LC_PCM_OK);
  nanosleep(&delay, NULL);
  assert(lc_pcm_recorder_finish(recorder, false, &wav, &length) == LC_PCM_OK);
  assert(length > 44);
  lc_pcm_recorder_destroy(recorder);

  for (int mode = 1; mode <= 6; mode++)
    {
      if (mode == 3) continue;
      reset(mode);
      recorder = lc_pcm_recorder_create(&ops, 40);
      assert(lc_pcm_recorder_start(recorder) == LC_PCM_OK);
      delay.tv_nsec = 80000000;
      nanosleep(&delay, NULL);
      assert(lc_pcm_recorder_finish(recorder, false, &wav, &length) == LC_PCM_DEVICE_ERROR);
      assert(wav == NULL && length == 0 && device.closes == (mode == 1 ? 0 : 1));
      lc_pcm_recorder_destroy(recorder);
    }
  puts("PASS: bounded asynchronous PCM recorder/WAV, cancellation, blocked reads and errors");
  return 0;
}
