#include "lc_audio_transport.h"

#include <string.h>

#ifdef __NuttX__
#  include <voice/voice_channel.h>
#endif

bool lc_audio_transport_init(lc_audio_transport_t *transport,
                             const lc_audio_transport_ops_t *ops)
{
  if (transport == NULL || ops == NULL || ops->start == NULL ||
      ops->stop_with_text == NULL || ops->speak == NULL)
    {
      return false;
    }

  memset(transport, 0, sizeof(*transport));
  transport->ops = *ops;
  return true;
}

bool lc_audio_transport_is_recording(const lc_audio_transport_t *transport)
{
  return transport != NULL && transport->recording;
}

lc_audio_status_t lc_audio_transport_start(lc_audio_transport_t *transport)
{
  if (transport == NULL || transport->ops.start == NULL)
    {
      return LC_AUDIO_INVALID;
    }

  if (transport->recording)
    {
      return LC_AUDIO_BUSY;
    }

  if (transport->ops.start() != 0)
    {
      return LC_AUDIO_BACKEND_ERROR;
    }

  transport->recording = true;
  return LC_AUDIO_OK;
}

lc_audio_status_t lc_audio_transport_stop(lc_audio_transport_t *transport,
                                          char *text,
                                          size_t capacity)
{
  int result;

  if (transport == NULL || text == NULL || capacity == 0u ||
      transport->ops.stop_with_text == NULL)
    {
      return LC_AUDIO_INVALID;
    }

  text[0] = '\0';
  if (!transport->recording)
    {
      return LC_AUDIO_NOT_RECORDING;
    }

  result = transport->ops.stop_with_text(text, capacity);
  transport->recording = false;
  if (result != 0)
    {
      text[0] = '\0';
      return LC_AUDIO_BACKEND_ERROR;
    }

  if (text[0] == '\0')
    {
      return LC_AUDIO_EMPTY;
    }

  return LC_AUDIO_OK;
}

lc_audio_status_t lc_audio_transport_speak(lc_audio_transport_t *transport,
                                           const char *text)
{
  if (transport == NULL || text == NULL || text[0] == '\0' ||
      transport->ops.speak == NULL)
    {
      return LC_AUDIO_INVALID;
    }

  return transport->ops.speak(text) == 0 ? LC_AUDIO_OK
                                         : LC_AUDIO_BACKEND_ERROR;
}

#ifdef __NuttX__
bool lc_audio_transport_init_default(lc_audio_transport_t *transport)
{
  const lc_audio_transport_ops_t ops =
  {
    .start = voice_channel_start,
    .stop_with_text = voice_channel_stop_with_text,
    .speak = voice_channel_speak
  };

  return lc_audio_transport_init(transport, &ops);
}
#endif
