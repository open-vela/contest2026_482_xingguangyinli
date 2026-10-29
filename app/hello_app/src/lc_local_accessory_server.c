#include "lc_local_accessory_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define LC_LOCAL_ACCESSORY_KEY_LINE_BYTES \
  (6u + LC_LOCAL_ACCESSORY_TAG_HEX_LENGTH + 1u)
#define LC_LOCAL_ACCESSORY_KEY_DOCUMENT_BYTES \
  (LC_LOCAL_ACCESSORY_KEY_LINE_BYTES * 2u)

static int lower_hex_value(char value)
{
  if (value >= '0' && value <= '9')
    {
      return value - '0';
    }
  if (value >= 'a' && value <= 'f')
    {
      return value - 'a' + 10;
    }
  return -1;
}

static bool decode_key(const char *hex,
                       uint8_t key[LC_LOCAL_ACCESSORY_KEY_BYTES])
{
  size_t index;

  for (index = 0u; index < LC_LOCAL_ACCESSORY_KEY_BYTES; index++)
    {
      const int high = lower_hex_value(hex[index * 2u]);
      const int low = lower_hex_value(hex[index * 2u + 1u]);
      if (high < 0 || low < 0)
        {
          return false;
        }
      key[index] = (uint8_t)((high << 4) | low);
    }
  return true;
}

bool lc_local_accessory_keys_parse(const char *document,
                                   size_t document_length,
                                   lc_local_accessory_keys_t *keys)
{
  const char *cat_b;

  if (document == NULL || keys == NULL ||
      document_length != LC_LOCAL_ACCESSORY_KEY_DOCUMENT_BYTES ||
      memcmp(document, "cat_a=", 6u) != 0 ||
      document[6u + LC_LOCAL_ACCESSORY_TAG_HEX_LENGTH] != '\n')
    {
      return false;
    }
  cat_b = document + LC_LOCAL_ACCESSORY_KEY_LINE_BYTES;
  if (memcmp(cat_b, "cat_b=", 6u) != 0 ||
      cat_b[6u + LC_LOCAL_ACCESSORY_TAG_HEX_LENGTH] != '\n')
    {
      return false;
    }

  memset(keys, 0, sizeof(*keys));
  if (!decode_key(document + 6u, keys->cat_a) ||
      !decode_key(cat_b + 6u, keys->cat_b))
    {
      memset(keys, 0, sizeof(*keys));
      return false;
    }
  return true;
}

bool lc_local_accessory_keys_load(const char *path,
                                  lc_local_accessory_keys_t *keys)
{
  char document[LC_LOCAL_ACCESSORY_KEY_DOCUMENT_BYTES];
  FILE *stream;
  size_t count;
  int trailing;
  bool valid;

  if (path == NULL || path[0] != '/' || keys == NULL)
    {
      return false;
    }
  stream = fopen(path, "rb");
  if (stream == NULL)
    {
      return false;
    }
  count = fread(document, 1u, sizeof(document), stream);
  trailing = fgetc(stream);
  valid = !ferror(stream) && count == sizeof(document) && trailing == EOF &&
          lc_local_accessory_keys_parse(document, count, keys);
  fclose(stream);
  memset(document, 0, sizeof(document));
  if (!valid)
    {
      memset(keys, 0, sizeof(*keys));
    }
  return valid;
}

static int posix_open(void *context, uint16_t port, uint32_t timeout_ms)
{
  struct sockaddr_in address;
  struct timeval timeout;
  int handle;

  (void)context;
  handle = socket(AF_INET, SOCK_DGRAM, 0);
  if (handle < 0)
    {
      return -1;
    }

  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  timeout.tv_sec = (time_t)(timeout_ms / 1000u);
  timeout.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);

  if (setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                 sizeof(timeout)) != 0 ||
      bind(handle, (const struct sockaddr *)&address, sizeof(address)) != 0)
    {
      close(handle);
      return -1;
    }
  return handle;
}

static int posix_receive(void *context, int handle, uint8_t *packet,
                         size_t packet_capacity, uint8_t *peer,
                         size_t *peer_length)
{
  socklen_t address_length;
  ssize_t received;

  (void)context;
  if (packet == NULL || peer == NULL || peer_length == NULL ||
      *peer_length < sizeof(struct sockaddr_in))
    {
      return LC_LOCAL_ACCESSORY_IO_ERROR;
    }
  address_length = (socklen_t)*peer_length;
  received = recvfrom(handle, packet, packet_capacity, 0,
                      (struct sockaddr *)peer, &address_length);
  if (received > 0)
    {
      *peer_length = (size_t)address_length;
      return (int)received;
    }
  if (received == 0 || errno == EAGAIN || errno == EWOULDBLOCK)
    {
      return LC_LOCAL_ACCESSORY_IO_TIMEOUT;
    }
  if (errno == EINTR)
    {
      return LC_LOCAL_ACCESSORY_IO_INTERRUPTED;
    }
  return LC_LOCAL_ACCESSORY_IO_ERROR;
}

static bool posix_send(void *context, int handle, const uint8_t *packet,
                       size_t packet_length, const uint8_t *peer,
                       size_t peer_length)
{
  ssize_t sent;

  (void)context;
  if (packet == NULL || peer == NULL || peer_length == 0u ||
      peer_length > LC_LOCAL_ACCESSORY_PEER_MAX)
    {
      return false;
    }
  sent = sendto(handle, packet, packet_length, 0,
                (const struct sockaddr *)peer, (socklen_t)peer_length);
  return sent >= 0 && (size_t)sent == packet_length;
}

static void posix_close(void *context, int handle)
{
  (void)context;
  close(handle);
}

static bool socket_ops_empty(const lc_local_accessory_socket_ops_t *ops)
{
  return ops->open == NULL && ops->receive == NULL &&
         ops->send == NULL && ops->close == NULL;
}

static bool socket_ops_complete(const lc_local_accessory_socket_ops_t *ops)
{
  return ops->open != NULL && ops->receive != NULL &&
         ops->send != NULL && ops->close != NULL;
}

bool lc_local_accessory_server_init(
  lc_local_accessory_server_t *server,
  const lc_local_accessory_server_config_t *config)
{
  if (server == NULL || config == NULL || config->port < 1024u ||
      config->receive_timeout_ms == 0u || config->hmac == NULL ||
      config->on_event == NULL ||
      (!socket_ops_empty(&config->socket_ops) &&
       !socket_ops_complete(&config->socket_ops)))
    {
      return false;
    }

  memset(server, 0, sizeof(*server));
  if (!lc_local_accessory_guard_init(&server->guard,
                                     config->cat_a_key,
                                     config->cat_b_key,
                                     config->hmac,
                                     config->hmac_context))
    {
      return false;
    }

  server->socket_ops = config->socket_ops;
  if (socket_ops_empty(&server->socket_ops))
    {
      server->socket_ops.open = posix_open;
      server->socket_ops.receive = posix_receive;
      server->socket_ops.send = posix_send;
      server->socket_ops.close = posix_close;
    }
  server->socket_context = config->socket_context;
  server->on_event = config->on_event;
  server->event_context = config->event_context;
  server->port = config->port;
  server->receive_timeout_ms = config->receive_timeout_ms;
  server->handle = -1;
  server->initialized = true;
  return true;
}

bool lc_local_accessory_server_start(lc_local_accessory_server_t *server)
{
  if (server == NULL || !server->initialized || server->started)
    {
      return false;
    }
  server->handle = server->socket_ops.open(server->socket_context,
                                           server->port,
                                           server->receive_timeout_ms);
  if (server->handle < 0)
    {
      server->handle = -1;
      return false;
    }
  server->stop_requested = false;
  server->started = true;
  return true;
}

lc_local_accessory_server_status_t lc_local_accessory_server_poll(
  lc_local_accessory_server_t *server)
{
  uint8_t packet[LC_LOCAL_ACCESSORY_PACKET_MAX];
  uint8_t peer[LC_LOCAL_ACCESSORY_PEER_MAX];
  char acknowledgement[LC_LOCAL_ACCESSORY_PACKET_MAX];
  lc_local_accessory_event_t event;
  lc_local_accessory_status_t verification;
  char prior_session[LC_LOCAL_ACCESSORY_DEVICE_COUNT]
                    [LC_LOCAL_ACCESSORY_SESSION_MAX];
  uint32_t prior_sequence[LC_LOCAL_ACCESSORY_DEVICE_COUNT];
  bool prior_valid[LC_LOCAL_ACCESSORY_DEVICE_COUNT];
  size_t peer_length = sizeof(peer);
  size_t device_index;
  int received;

  if (server == NULL || !server->initialized || !server->started)
    {
      return LC_LOCAL_ACCESSORY_SERVER_IO_ERROR;
    }
  if (server->stop_requested)
    {
      lc_local_accessory_server_stop(server);
      return LC_LOCAL_ACCESSORY_SERVER_STOPPED;
    }

  received = server->socket_ops.receive(server->socket_context,
                                         server->handle,
                                         packet, sizeof(packet),
                                         peer, &peer_length);
  if (received == LC_LOCAL_ACCESSORY_IO_TIMEOUT ||
      received == LC_LOCAL_ACCESSORY_IO_INTERRUPTED)
    {
      return LC_LOCAL_ACCESSORY_SERVER_IDLE;
    }
  if (received < 0)
    {
      lc_local_accessory_server_stop(server);
      return LC_LOCAL_ACCESSORY_SERVER_IO_ERROR;
    }
  if ((size_t)received >= sizeof(packet) || peer_length == 0u ||
      peer_length > sizeof(peer))
    {
      return LC_LOCAL_ACCESSORY_SERVER_REJECTED;
    }

  for (device_index = 0u;
       device_index < LC_LOCAL_ACCESSORY_DEVICE_COUNT;
       device_index++)
    {
      memcpy(prior_session[device_index],
             server->guard.cursor[device_index].session_id,
             LC_LOCAL_ACCESSORY_SESSION_MAX);
      prior_sequence[device_index] =
        server->guard.cursor[device_index].last_sequence;
      prior_valid[device_index] = server->guard.cursor[device_index].valid;
    }

  verification = lc_local_accessory_verify(&server->guard, packet,
                                            (size_t)received, &event);
  if (verification != LC_LOCAL_ACCESSORY_ACCEPTED &&
      verification != LC_LOCAL_ACCESSORY_DUPLICATE)
    {
      return LC_LOCAL_ACCESSORY_SERVER_REJECTED;
    }
  if (verification == LC_LOCAL_ACCESSORY_ACCEPTED &&
      !server->on_event(server->event_context, &event))
    {
      memcpy(server->guard.cursor[event.device].session_id,
             prior_session[event.device], LC_LOCAL_ACCESSORY_SESSION_MAX);
      server->guard.cursor[event.device].last_sequence =
        prior_sequence[event.device];
      server->guard.cursor[event.device].valid = prior_valid[event.device];
      return LC_LOCAL_ACCESSORY_SERVER_DELIVERY_FAILED;
    }
  if (!lc_local_accessory_ack_format(&server->guard, &event, verification,
                                     acknowledgement,
                                     sizeof(acknowledgement)) ||
      !server->socket_ops.send(server->socket_context, server->handle,
                               (const uint8_t *)acknowledgement,
                               strlen(acknowledgement), peer, peer_length))
    {
      return LC_LOCAL_ACCESSORY_SERVER_IO_ERROR;
    }
  return verification == LC_LOCAL_ACCESSORY_ACCEPTED
           ? LC_LOCAL_ACCESSORY_SERVER_ACCEPTED
           : LC_LOCAL_ACCESSORY_SERVER_DUPLICATE;
}

void lc_local_accessory_server_request_stop(
  lc_local_accessory_server_t *server)
{
  if (server != NULL && server->initialized)
    {
      server->stop_requested = true;
    }
}

void lc_local_accessory_server_stop(lc_local_accessory_server_t *server)
{
  if (server == NULL || !server->initialized || !server->started)
    {
      return;
    }
  server->socket_ops.close(server->socket_context, server->handle);
  server->handle = -1;
  server->started = false;
}
