#define _POSIX_C_SOURCE 200809L
#include "lc_native_voice.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct lc_native_voice
{
  lc_pcm_recorder_t *recorder;
  lc_voice_client_t client;
  lc_pcm_player_ops_t player;
  void *player_context;
  pthread_mutex_t operation;
  pthread_mutex_t state;
  uint64_t generation;
  uint64_t capture_ticket;
  bool recording;
  bool native_player;
  char session[LC_PRODUCT_SESSION_MAX];
};
typedef struct
{
  lc_native_voice_t *voice;
  uint64_t ticket;
  const char *session;
} voice_job_t;

static void wipe(void *data, size_t size)
{
  volatile unsigned char *p = data;
  while (size-- != 0u) *p++ = 0u;
}
static bool valid_session(const char *id)
{
  if (id == NULL || strnlen(id, 18u) != 17u ||
      strncmp(id, "meal_", 5u) != 0) return false;
  for (size_t i = 5u; i < 17u; ++i)
    if (!((id[i] >= '0' && id[i] <= '9') ||
          (id[i] >= 'a' && id[i] <= 'f'))) return false;
  return true;
}
bool lc_native_voice_current(lc_native_voice_t *voice, uint64_t ticket)
{
  if (voice == NULL || ticket == 0u) return false;
  pthread_mutex_lock(&voice->state);
  bool current = voice->generation == ticket;
  pthread_mutex_unlock(&voice->state);
  return current;
}
static bool job_current(void *context)
{
  voice_job_t *job = context;
  return lc_native_voice_current(job->voice, job->ticket);
}
static bool session_current(void *context, const char *id)
{
  voice_job_t *job = context;
  return strcmp(job->session, id) == 0 && job_current(context);
}
void lc_native_voice_cancel(lc_native_voice_t *voice)
{
  if (voice == NULL) return;
  pthread_mutex_lock(&voice->state);
  /* Saturation refuses future starts, rather than reviving an old ticket. */
  if (voice->generation != UINT64_MAX) ++voice->generation;
  pthread_mutex_unlock(&voice->state);
}
lc_native_voice_t *lc_native_voice_create(lc_pcm_recorder_t *recorder,
  const lc_voice_client_t *client, const lc_pcm_player_ops_t *player,
  void *player_context)
{
  if (recorder == NULL || client == NULL || !client->configured) return NULL;
  lc_native_voice_t *voice = calloc(1u, sizeof(*voice));
  if (voice == NULL) return NULL;
  if (pthread_mutex_init(&voice->operation, NULL) != 0)
    { free(voice); return NULL; }
  if (pthread_mutex_init(&voice->state, NULL) != 0)
    { pthread_mutex_destroy(&voice->operation); free(voice); return NULL; }
  voice->recorder = recorder;
  voice->client = *client;
  voice->native_player = player == NULL;
  if (player != NULL) voice->player = *player;
  voice->player_context = player_context;
  return voice;
}
lc_native_voice_status_t lc_native_voice_start(lc_native_voice_t *voice,
  const char *existing_session, uint64_t *ticket)
{
  if (ticket != NULL) *ticket = 0u;
  if (voice == NULL || ticket == NULL ||
      (existing_session != NULL && !valid_session(existing_session)))
    return LC_NATIVE_VOICE_INVALID;
  if (pthread_mutex_trylock(&voice->operation) != 0) return LC_NATIVE_VOICE_BUSY;
  pthread_mutex_lock(&voice->state);
  if (voice->recording || voice->generation == UINT64_MAX)
    {
      pthread_mutex_unlock(&voice->state);
      pthread_mutex_unlock(&voice->operation);
      return LC_NATIVE_VOICE_BUSY;
    }
  voice->capture_ticket = ++voice->generation;
  memset(voice->session, 0, sizeof(voice->session));
  if (existing_session != NULL) memcpy(voice->session, existing_session, 18u);
  pthread_mutex_unlock(&voice->state);
  lc_pcm_status_t capture = lc_pcm_recorder_start(voice->recorder);
  pthread_mutex_lock(&voice->state);
  voice->recording = capture == LC_PCM_OK;
  if (voice->recording) *ticket = voice->capture_ticket;
  pthread_mutex_unlock(&voice->state);
  pthread_mutex_unlock(&voice->operation);
  return capture == LC_PCM_OK ? LC_NATIVE_VOICE_OK : LC_NATIVE_VOICE_CAPTURE_ERROR;
}
lc_native_voice_status_t lc_native_voice_finish(lc_native_voice_t *voice,
  uint64_t ticket, char *session, size_t session_capacity,
  char *transcript, size_t transcript_capacity)
{
  if (session != NULL && session_capacity != 0u) session[0] = '\0';
  if (transcript != NULL && transcript_capacity != 0u) transcript[0] = '\0';
  if (voice == NULL || session == NULL || transcript == NULL ||
      session_capacity < LC_PRODUCT_SESSION_MAX ||
      transcript_capacity < LC_VOICE_TRANSCRIPT_BYTES) return LC_NATIVE_VOICE_INVALID;
  if (pthread_mutex_trylock(&voice->operation) != 0) return LC_NATIVE_VOICE_BUSY;
  pthread_mutex_lock(&voice->state);
  bool recording = voice->recording && voice->capture_ticket == ticket;
  char id[LC_PRODUCT_SESSION_MAX];
  memcpy(id, voice->session, sizeof(id));
  if (recording) voice->recording = false;
  pthread_mutex_unlock(&voice->state);
  if (!recording)
    { pthread_mutex_unlock(&voice->operation); return LC_NATIVE_VOICE_INVALID; }
  voice_job_t job = {voice, ticket, id};
  const uint8_t *wav = NULL;
  size_t length = 0u;
  lc_pcm_status_t capture = lc_pcm_recorder_finish(voice->recorder,
    !job_current(&job), &wav, &length);
  lc_native_voice_status_t result = LC_NATIVE_VOICE_CAPTURE_ERROR;
  if (!job_current(&job)) result = LC_NATIVE_VOICE_CANCELLED;
  else if (capture == LC_PCM_OK)
    {
      lc_voice_client_status_t service = LC_VOICE_CLIENT_OK;
      if (id[0] == '\0')
        service = lc_voice_client_open_session(&voice->client, id, sizeof(id));
      if (!job_current(&job)) result = LC_NATIVE_VOICE_CANCELLED;
      else if (service != LC_VOICE_CLIENT_OK) result = LC_NATIVE_VOICE_SERVICE_ERROR;
      else
        {
          service = lc_voice_client_transcribe(&voice->client, id, wav, length,
            transcript, transcript_capacity, session_current, &job);
          result = service == LC_VOICE_CLIENT_OK ? LC_NATIVE_VOICE_OK :
            (service == LC_VOICE_CLIENT_CANCELLED ? LC_NATIVE_VOICE_CANCELLED :
                                                  LC_NATIVE_VOICE_SERVICE_ERROR);
        }
    }
  (void)lc_pcm_recorder_discard(voice->recorder);
  pthread_mutex_lock(&voice->state);
  if (voice->generation != ticket) result = LC_NATIVE_VOICE_CANCELLED;
  if (result == LC_NATIVE_VOICE_OK) memcpy(session, id, strlen(id) + 1u);
  else wipe(transcript, transcript_capacity);
  pthread_mutex_unlock(&voice->state);
  wipe(id, sizeof(id));
  pthread_mutex_unlock(&voice->operation);
  return result;
}
lc_native_voice_status_t lc_native_voice_speak(lc_native_voice_t *voice,
  const char *session, const char *text)
{
  if (voice == NULL || !valid_session(session) || text == NULL)
    return LC_NATIVE_VOICE_INVALID;
  if (pthread_mutex_trylock(&voice->operation) != 0) return LC_NATIVE_VOICE_BUSY;
  pthread_mutex_lock(&voice->state);
  if (voice->recording || voice->generation == UINT64_MAX)
    {
      pthread_mutex_unlock(&voice->state);
      pthread_mutex_unlock(&voice->operation);
      return LC_NATIVE_VOICE_BUSY;
    }
  voice_job_t job = {voice, ++voice->generation, session};
  pthread_mutex_unlock(&voice->state);
  size_t capacity = LC_VOICE_SYNTHESIS_MAX + 2u, length = 0u;
  uint8_t *wav = malloc(capacity);
  lc_native_voice_status_t result = LC_NATIVE_VOICE_SERVICE_ERROR;
  if (wav != NULL)
    {
      lc_voice_client_status_t service = lc_voice_client_synthesize(&voice->client,
        session, text, wav, capacity, &length, session_current, &job);
      if (!job_current(&job)) result = LC_NATIVE_VOICE_CANCELLED;
      else if (service == LC_VOICE_CLIENT_OK)
        {
          lc_pcm_player_status_t played;
          if (!voice->native_player)
            played = lc_pcm_player_play(&voice->player, voice->player_context,
              wav + 44u, length - 44u, job_current, &job, 20000u);
          else
            {
#ifdef __NuttX__
              played = lc_pcm_player_play_native(wav + 44u, length - 44u,
                                                job_current, &job);
#else
              played = LC_PCM_PLAYER_UNAVAILABLE;
#endif
            }
          result = played == LC_PCM_PLAYER_OK ? LC_NATIVE_VOICE_OK :
            (played == LC_PCM_PLAYER_CANCELLED ? LC_NATIVE_VOICE_CANCELLED :
                                               LC_NATIVE_VOICE_PLAY_ERROR);
        }
      wipe(wav, capacity);
      free(wav);
    }
  if (!job_current(&job)) result = LC_NATIVE_VOICE_CANCELLED;
  pthread_mutex_unlock(&voice->operation);
  return result;
}
void lc_native_voice_destroy(lc_native_voice_t *voice)
{
  if (voice == NULL) return;
  lc_native_voice_cancel(voice);
  pthread_mutex_lock(&voice->operation);
  lc_pcm_recorder_destroy(voice->recorder);
  lc_voice_client_clear(&voice->client);
  pthread_mutex_unlock(&voice->operation);
  pthread_mutex_destroy(&voice->operation);
  pthread_mutex_destroy(&voice->state);
  wipe(voice, sizeof(*voice));
  free(voice);
}
#ifdef __NuttX__
lc_native_voice_t *lc_native_voice_create_default(
  const lc_product_transport_t *backend, unsigned int max_ms)
{
  lc_voice_client_t client;
  if (!lc_voice_client_init_default(&client, backend)) return NULL;
  lc_pcm_recorder_t *recorder = lc_pcm_recorder_create_native(max_ms);
  lc_native_voice_t *voice = lc_native_voice_create(recorder, &client, NULL, NULL);
  lc_voice_client_clear(&client);
  if (voice == NULL) lc_pcm_recorder_destroy(recorder);
  return voice;
}
#endif
