#define _POSIX_C_SOURCE 200809L
#include "lc_pcm_player.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct
{
  lc_pcm_player_notify_fn notify;
  void *cookie;
  pthread_t thread;
  bool thread_started;
  bool current;
  bool open_fail;
  bool write_fail;
  bool end_fail;
  bool close_fail;
  bool missing_done;
  bool early_done;
  bool failed_event;
  bool cancel_on_write;
  bool backpressure;
  size_t written;
  int opens, writes, ends, stops, closes;
  bool delivered;
} fixture_t;
static const uint8_t pcm[] = {0x21,0,0x32,0,0x43,0,0x54,0};

static void pause_ms(unsigned int ms)
{
  struct timespec delay = {(time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L};
  (void)nanosleep(&delay, NULL);
}

static void *complete(void *context)
{
  fixture_t *f = context;
  pause_ms(20u);
  f->notify(f->cookie, f->failed_event);
  f->delivered = true;
  return NULL;
}

static bool is_current(void *context)
{
  return ((fixture_t *)context)->current;
}

static void *open_device(void *context, lc_pcm_player_notify_fn notify, void *cookie)
{
  fixture_t *f = context;
  f->opens++;
  f->notify = notify;
  f->cookie = cookie;
  return f->open_fail ? NULL : f;
}

static int write_device(void *context, void *handle, const uint8_t *data, size_t length)
{
  fixture_t *f = context;
  assert(handle == f && length > 0u);
  f->writes++;
  if (f->write_fail) return -1;
  if (f->cancel_on_write) f->current = false;
  if (f->early_done) f->notify(f->cookie, false);
  if (f->backpressure && f->writes == 1) return 0;
  size_t n = length > 3u ? 3u : length; /* Actual partial bytes, no dropped tail. */
  assert(memcmp(data, pcm + f->written, n) == 0);
  f->written += n;
  return (int)n;
}

static int end_device(void *context, void *handle)
{
  fixture_t *f = context;
  assert(handle == f && f->written == sizeof(pcm));
  f->ends++;
  if (f->end_fail) return -1;
  if (!f->missing_done)
    {
      assert(pthread_create(&f->thread, NULL, complete, f) == 0);
      f->thread_started = true;
    }
  return 0;
}

static void stop_device(void *context, void *handle)
{
  fixture_t *f = context;
  assert(handle == f);
  f->stops++;
}

static int close_device(void *context, void *handle)
{
  fixture_t *f = context;
  assert(handle == f);
  f->closes++;
  if (f->thread_started) assert(pthread_join(f->thread, NULL) == 0);
  return f->close_fail ? -1 : 0;
}

static lc_pcm_player_status_t play(fixture_t *f, unsigned int timeout)
{
  lc_pcm_player_ops_t ops = {open_device, write_device, end_device, stop_device, close_device};
  return lc_pcm_player_play(&ops, f, pcm, sizeof(pcm), is_current, f, timeout);
}

int main(void)
{
  fixture_t f = {.current=true, .backpressure=true};
  assert(play(&f, 200u) == LC_PCM_PLAYER_OK);
  assert(f.delivered && f.written == sizeof(pcm));
  assert(f.ends == 1 && f.stops == 0 && f.closes == 1);
  f = (fixture_t){.current=false};
  assert(play(&f, 200u) == LC_PCM_PLAYER_CANCELLED && f.opens == 0);
  f = (fixture_t){.current=true, .cancel_on_write=true};
  assert(play(&f, 200u) == LC_PCM_PLAYER_CANCELLED);
  assert(f.ends == 0 && f.stops == 1 && f.closes == 1);
  f = (fixture_t){.current=true, .early_done=true};
  assert(play(&f, 200u) == LC_PCM_PLAYER_DEVICE_ERROR);
  assert(f.ends == 0 && f.stops == 1 && f.closes == 1);
  f = (fixture_t){.current=true, .missing_done=true};
  assert(play(&f, 30u) == LC_PCM_PLAYER_TIMEOUT);
  assert(f.written == sizeof(pcm) && f.ends == 1 && f.stops == 1);
  f = (fixture_t){.current=true, .failed_event=true};
  assert(play(&f, 200u) == LC_PCM_PLAYER_DEVICE_ERROR && f.closes == 1);
  f = (fixture_t){.current=true, .open_fail=true};
  assert(play(&f, 200u) == LC_PCM_PLAYER_DEVICE_ERROR && f.closes == 0);
  f = (fixture_t){.current=true, .write_fail=true};
  assert(play(&f, 200u) == LC_PCM_PLAYER_DEVICE_ERROR && f.ends == 0 && f.closes == 1);
  f = (fixture_t){.current=true, .end_fail=true};
  assert(play(&f, 200u) == LC_PCM_PLAYER_DEVICE_ERROR && f.stops == 1 && f.closes == 1);
  lc_pcm_player_ops_t ops = {open_device, write_device, end_device, stop_device, close_device};
  assert(lc_pcm_player_play(&ops, &f, pcm, 3u, is_current, &f, 200u) == LC_PCM_PLAYER_INVALID);
  assert(lc_pcm_player_play(&ops, &f, pcm, 720002u, is_current, &f, 200u) == LC_PCM_PLAYER_INVALID);
  assert(lc_pcm_player_play(&ops, &f, pcm, sizeof(pcm), is_current, &f, 0u) == LC_PCM_PLAYER_INVALID);
  assert(lc_pcm_player_play(&ops, &f, pcm, sizeof(pcm), NULL, &f, 200u) == LC_PCM_PLAYER_INVALID);
  /* Last: failed close may still call its cookie. Keep it live, fail closed. */
  f = (fixture_t){.current=true, .close_fail=true};
  assert(play(&f, 200u) == LC_PCM_PLAYER_DEVICE_ERROR);
  f.notify(f.cookie, false); /* Must not touch freed callback memory. */
  int previous = f.opens;
  assert(play(&f, 200u) == LC_PCM_PLAYER_UNAVAILABLE && f.opens == previous);
  puts("PASS: PCM waits for completion/partial writes/cancel/timeout/errors/close-failure lifetime");
  return 0;
}
