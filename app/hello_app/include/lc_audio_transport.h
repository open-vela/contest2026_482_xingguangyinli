#ifndef LIVING_CANVAS_LC_AUDIO_TRANSPORT_H
#define LIVING_CANVAS_LC_AUDIO_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>

typedef enum
{
  LC_AUDIO_OK = 0,
  LC_AUDIO_BUSY,
  LC_AUDIO_NOT_RECORDING,
  LC_AUDIO_EMPTY,
  LC_AUDIO_BACKEND_ERROR,
  LC_AUDIO_INVALID
} lc_audio_status_t;

typedef struct
{
  int (*start)(void);
  int (*stop_with_text)(char *text, size_t capacity);
  int (*speak)(const char *text);
} lc_audio_transport_ops_t;

typedef struct
{
  lc_audio_transport_ops_t ops;
  bool recording;
} lc_audio_transport_t;

bool lc_audio_transport_init(lc_audio_transport_t *transport,
                             const lc_audio_transport_ops_t *ops);
bool lc_audio_transport_is_recording(const lc_audio_transport_t *transport);
lc_audio_status_t lc_audio_transport_start(lc_audio_transport_t *transport);
lc_audio_status_t lc_audio_transport_stop(lc_audio_transport_t *transport,
                                          char *text,
                                          size_t capacity);
lc_audio_status_t lc_audio_transport_speak(lc_audio_transport_t *transport,
                                           const char *text);

#ifdef __NuttX__
bool lc_audio_transport_init_default(lc_audio_transport_t *transport);
#endif

#endif
