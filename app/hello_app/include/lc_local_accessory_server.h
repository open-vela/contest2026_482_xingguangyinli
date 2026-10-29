#ifndef LIVING_CANVAS_LC_LOCAL_ACCESSORY_SERVER_H
#define LIVING_CANVAS_LC_LOCAL_ACCESSORY_SERVER_H

#include "lc_local_accessory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LC_LOCAL_ACCESSORY_PEER_MAX 32u
#define LC_LOCAL_ACCESSORY_KEYS_CONFIG_PATH \
  "/data/living_canvas/local_accessory_keys"
#define LC_LOCAL_ACCESSORY_IO_ERROR (-1)
#define LC_LOCAL_ACCESSORY_IO_INTERRUPTED (-2)
#define LC_LOCAL_ACCESSORY_IO_TIMEOUT 0

typedef int (*lc_local_accessory_socket_open_fn)(void *context,
                                                 uint16_t port,
                                                 uint32_t timeout_ms);
typedef int (*lc_local_accessory_socket_receive_fn)(
  void *context, int handle, uint8_t *packet, size_t packet_capacity,
  uint8_t *peer, size_t *peer_length);
typedef bool (*lc_local_accessory_socket_send_fn)(
  void *context, int handle, const uint8_t *packet, size_t packet_length,
  const uint8_t *peer, size_t peer_length);
typedef void (*lc_local_accessory_socket_close_fn)(void *context,
                                                   int handle);

typedef struct
{
  lc_local_accessory_socket_open_fn open;
  lc_local_accessory_socket_receive_fn receive;
  lc_local_accessory_socket_send_fn send;
  lc_local_accessory_socket_close_fn close;
} lc_local_accessory_socket_ops_t;

typedef bool (*lc_local_accessory_event_fn)(
  void *context, const lc_local_accessory_event_t *event);

typedef struct
{
  uint8_t cat_a[LC_LOCAL_ACCESSORY_KEY_BYTES];
  uint8_t cat_b[LC_LOCAL_ACCESSORY_KEY_BYTES];
} lc_local_accessory_keys_t;

typedef struct
{
  uint16_t port;
  uint32_t receive_timeout_ms;
  uint8_t cat_a_key[LC_LOCAL_ACCESSORY_KEY_BYTES];
  uint8_t cat_b_key[LC_LOCAL_ACCESSORY_KEY_BYTES];
  lc_local_accessory_hmac_fn hmac;
  void *hmac_context;
  lc_local_accessory_event_fn on_event;
  void *event_context;
  lc_local_accessory_socket_ops_t socket_ops;
  void *socket_context;
} lc_local_accessory_server_config_t;

typedef enum
{
  LC_LOCAL_ACCESSORY_SERVER_IDLE = 0,
  LC_LOCAL_ACCESSORY_SERVER_ACCEPTED,
  LC_LOCAL_ACCESSORY_SERVER_DUPLICATE,
  LC_LOCAL_ACCESSORY_SERVER_REJECTED,
  LC_LOCAL_ACCESSORY_SERVER_DELIVERY_FAILED,
  LC_LOCAL_ACCESSORY_SERVER_IO_ERROR,
  LC_LOCAL_ACCESSORY_SERVER_STOPPED
} lc_local_accessory_server_status_t;

typedef struct
{
  lc_local_accessory_guard_t guard;
  lc_local_accessory_socket_ops_t socket_ops;
  void *socket_context;
  lc_local_accessory_event_fn on_event;
  void *event_context;
  uint16_t port;
  uint32_t receive_timeout_ms;
  int handle;
  bool initialized;
  bool started;
  bool stop_requested;
} lc_local_accessory_server_t;

bool lc_local_accessory_server_init(
  lc_local_accessory_server_t *server,
  const lc_local_accessory_server_config_t *config);
bool lc_local_accessory_server_start(lc_local_accessory_server_t *server);
lc_local_accessory_server_status_t lc_local_accessory_server_poll(
  lc_local_accessory_server_t *server);
void lc_local_accessory_server_request_stop(
  lc_local_accessory_server_t *server);
void lc_local_accessory_server_stop(lc_local_accessory_server_t *server);
bool lc_local_accessory_keys_parse(const char *document,
                                   size_t document_length,
                                   lc_local_accessory_keys_t *keys);
bool lc_local_accessory_keys_load(const char *path,
                                  lc_local_accessory_keys_t *keys);

#endif
