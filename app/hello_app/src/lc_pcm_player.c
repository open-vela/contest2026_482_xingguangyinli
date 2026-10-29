#define _POSIX_C_SOURCE 200809L
#include "lc_pcm_player.h"
#include <pthread.h>
#include <stdlib.h>
#include <time.h>

typedef struct
{
  pthread_mutex_t lock;
  bool done;
  bool failed;
} completion_t;

static pthread_mutex_t owner = PTHREAD_MUTEX_INITIALIZER;
/* Only retained on failed close: callback may still reference it. */
static completion_t *retained;

static bool now_ms(uint64_t *value)
{
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0 || now.tv_sec < 0) return false;
  *value = (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
  return true;
}

static void completed(void *cookie, bool failed)
{
  completion_t *state = cookie;
  pthread_mutex_lock(&state->lock);
  state->done = true;
  state->failed = state->failed || failed;
  pthread_mutex_unlock(&state->lock);
}

lc_pcm_player_status_t lc_pcm_player_play(const lc_pcm_player_ops_t *ops,
  void *device_context, const uint8_t *pcm, size_t length,
  lc_pcm_player_current_fn current, void *context, unsigned int timeout_ms)
{
  if (ops == NULL || ops->open == NULL || ops->write == NULL ||
      ops->end == NULL || ops->stop == NULL || ops->close == NULL ||
      pcm == NULL || length == 0u || length > 720000u || length % 2u != 0u ||
      current == NULL || timeout_ms == 0u || timeout_ms > 20000u)
    return LC_PCM_PLAYER_INVALID;
  if (!current(context)) return LC_PCM_PLAYER_CANCELLED;
  if (pthread_mutex_trylock(&owner) != 0) return LC_PCM_PLAYER_UNAVAILABLE;
  if (retained != NULL)
    {
      pthread_mutex_unlock(&owner);
      return LC_PCM_PLAYER_UNAVAILABLE;
    }
  uint64_t started;
  if (!now_ms(&started))
    {
      pthread_mutex_unlock(&owner);
      return LC_PCM_PLAYER_DEVICE_ERROR;
    }
  completion_t *state = calloc(1u, sizeof(*state));
  if (state == NULL || pthread_mutex_init(&state->lock, NULL) != 0)
    {
      free(state);
      pthread_mutex_unlock(&owner);
      return LC_PCM_PLAYER_DEVICE_ERROR;
    }
  void *handle = ops->open(device_context, completed, state);
  lc_pcm_player_status_t status = LC_PCM_PLAYER_DEVICE_ERROR;
  size_t written = 0u;
  bool ended = false;
  while (handle != NULL)
    {
      if (!current(context)) { status = LC_PCM_PLAYER_CANCELLED; break; }
      uint64_t now;
      if (!now_ms(&now)) break;
      if (now - started >= timeout_ms) { status = LC_PCM_PLAYER_TIMEOUT; break; }
      pthread_mutex_lock(&state->lock);
      bool done = state->done;
      bool failed = state->failed;
      pthread_mutex_unlock(&state->lock);
      if (done)
        {
          if (!failed && ended) status = LC_PCM_PLAYER_OK;
          break;
        }
      if (written < length)
        {
          size_t remaining = length - written;
          size_t chunk = remaining > 4096u ? 4096u : remaining;
          int count = ops->write(device_context, handle, pcm + written, chunk);
          if (count < 0 || (size_t)count > chunk) break;
          if (count > 0)
            {
              written += (size_t)count;
              continue;
            }
        }
      else if (!ended)
        {
          ended = true; /* Completion may arrive synchronously from end(). */
          if (ops->end(device_context, handle) != 0) break;
          continue;
        }
      const struct timespec delay = {0, 10000000L};
      (void)nanosleep(&delay, NULL);
    }
  if (handle != NULL)
    {
      if (status != LC_PCM_PLAYER_OK) ops->stop(device_context, handle);
      if (ops->close(device_context, handle) != 0)
        {
          retained = state;
          status = LC_PCM_PLAYER_DEVICE_ERROR;
        }
    }
  if (retained == NULL)
    {
      pthread_mutex_destroy(&state->lock);
      free(state);
    }
  if (status == LC_PCM_PLAYER_OK && !current(context)) status = LC_PCM_PLAYER_CANCELLED;
  pthread_mutex_unlock(&owner);
  return status;
}

#ifdef __NuttX__
#include <errno.h>
#include <media_player.h>
#include <sys/socket.h>

typedef struct
{
  void *media;
  int fd;
  lc_pcm_player_notify_fn notify;
  void *cookie;
} native_player_t;

static void native_event(void *cookie, int event, int result, const char *extra)
{
  native_player_t *player = cookie;
  (void)extra;
  if (result < 0) player->notify(player->cookie, true);
  else if (event == MEDIA_EVENT_COMPLETED) player->notify(player->cookie, false);
}

static void *native_open(void *context, lc_pcm_player_notify_fn notify, void *cookie)
{
  (void)context;
  native_player_t *player = calloc(1u, sizeof(*player));
  if (player == NULL) return NULL;
  player->fd = -1;
  player->notify = notify;
  player->cookie = cookie;
  player->media = media_player_open(MEDIA_STREAM_MUSIC);
  if (player->media == NULL) { free(player); return NULL; }
  /* Return ownership even on setup error, so the common close path retains
   * callback state if the SDK fails to disconnect its event thread. */
  if (media_player_set_event_callback(player->media, player, native_event) < 0 ||
      media_player_prepare(player->media, NULL,
        "format=s16le:sample_rate=24000:ch_layout=mono") < 0 ||
      media_player_start(player->media) < 0)
    notify(cookie, true);
  return player;
}

static int native_write(void *context, void *handle, const uint8_t *bytes, size_t length)
{
  (void)context;
  native_player_t *player = handle;
  /* get_socket may block accepting the first connection: worker only. */
  if (player->fd < 0) player->fd = media_player_get_socket(player->media);
  if (player->fd < 0) return -1;
  ssize_t count = send(player->fd, bytes, length, MSG_DONTWAIT | MSG_NOSIGNAL);
  if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
  return count > 0 ? (int)count : -1;
}

static int native_end(void *context, void *handle)
{
  (void)context;
  native_player_t *player = handle;
  media_player_close_socket(player->media); /* EOF, do NOT stop queued playback. */
  player->fd = -1;
  return 0;
}

static void native_stop(void *context, void *handle)
{
  (void)context;
  native_player_t *player = handle;
  (void)media_player_stop(player->media);
}

static int native_close(void *context, void *handle)
{
  (void)context;
  native_player_t *player = handle;
  int result = media_player_close(player->media, 0);
  if (result >= 0) free(player); /* Successful SDK close joins event thread. */
  return result < 0 ? -1 : 0;
}

lc_pcm_player_status_t lc_pcm_player_play_native(const uint8_t *pcm, size_t length,
  lc_pcm_player_current_fn current, void *context)
{
  const lc_pcm_player_ops_t ops = {native_open, native_write, native_end, native_stop, native_close};
  return lc_pcm_player_play(&ops, NULL, pcm, length, current, context, 20000u);
}
#endif
