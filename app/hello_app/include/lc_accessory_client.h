#ifndef LIVING_CANVAS_LC_ACCESSORY_CLIENT_H
#define LIVING_CANVAS_LC_ACCESSORY_CLIENT_H

#include "lc_accessory_event.h"
#include "lc_product_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define LC_ACCESSORY_TOKEN_CONFIG_PATH \
  "/data/living_canvas/accessory_device_token"
#define LC_ACCESSORY_POLL_MAX 32u

typedef enum
{
  LC_ACCESSORY_CLIENT_OK = 0,
  LC_ACCESSORY_CLIENT_RESET,
  LC_ACCESSORY_CLIENT_INVALID,
  LC_ACCESSORY_CLIENT_NETWORK,
  LC_ACCESSORY_CLIENT_HTTP,
  LC_ACCESSORY_CLIENT_SCHEMA
} lc_accessory_client_status_t;

typedef int (*lc_accessory_request_fn)(
  void *context,
  const lc_product_transport_t *backend,
  const char *path,
  const char *authorization,
  char *response,
  size_t capacity,
  size_t *response_length);

typedef struct
{
  lc_product_transport_t backend;
  char authorization[136];
  uint32_t cursor;
  lc_accessory_request_fn request;
  void *context;
  bool configured;
} lc_accessory_client_t;

/* Owner-device relay token only. Never pass a provider API key. */
bool lc_accessory_client_init(
  lc_accessory_client_t *client,
  const lc_product_transport_t *backend,
  const char *device_token,
  lc_accessory_request_fn request,
  void *context);
bool lc_accessory_client_init_stream(
  lc_accessory_client_t *client,
  const lc_product_transport_t *backend,
  FILE *stream,
  lc_accessory_request_fn request,
  void *context);
void lc_accessory_client_clear(lc_accessory_client_t *client);

/* One bounded GET, no redirects and no automatic retry. On RESET the server
 * restarted, the local cursor becomes zero, and the caller should poll later.
 * Outputs are cleared on every error; cursor advances only after the complete
 * response is validated. Invoke from a worker, never from LVGL. */
lc_accessory_client_status_t lc_accessory_client_poll(
  lc_accessory_client_t *client,
  lc_accessory_event_t *events,
  size_t capacity,
  size_t *count,
  bool *history_truncated);

#ifdef __NuttX__
bool lc_accessory_client_init_default(
  lc_accessory_client_t *client,
  const lc_product_transport_t *backend);
#endif

#endif
