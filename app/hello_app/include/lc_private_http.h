#ifndef LC_PRIVATE_HTTP_H
#define LC_PRIVATE_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* This transport exists only as a roadshow fallback on an isolated private
 * LAN. Public destinations must continue to use the verified HTTPS path. */
enum
{
  LC_PRIVATE_HTTP_INVALID = -1,
  LC_PRIVATE_HTTP_CONNECT = -2,
  LC_PRIVATE_HTTP_WRITE = -3,
  LC_PRIVATE_HTTP_READ = -4,
  LC_PRIVATE_HTTP_OVERFLOW = -5,
  LC_PRIVATE_HTTP_PROTOCOL = -6
};

typedef struct
{
  const char *name;
  const char *value;
} lc_private_http_header_t;

typedef struct
{
  intptr_t (*open)(void *context, const char *host, unsigned int port);
  int (*send)(void *context, intptr_t handle,
              const void *bytes, size_t length);
  int (*receive)(void *context, intptr_t handle,
                 void *bytes, size_t capacity);
  void (*close)(void *context, intptr_t handle);
} lc_private_http_ops_t;

bool lc_private_http_host_allowed(const char *host);

/* POST-only, binary-safe, Content-Length framed, no redirect/chunked support.
 * Host must be an RFC1918 IPv4 literal and port must be 1024..65535. */
int lc_private_http_request(
  const char *host, const char *port, const char *method, const char *path,
  const lc_private_http_header_t *headers,
  const void *body, size_t body_length,
  void *response, size_t response_capacity, size_t *response_length);

/* Deterministic I/O boundary for host tests; same validation as production. */
int lc_private_http_request_with_ops(
  const lc_private_http_ops_t *ops, void *context,
  const char *host, const char *port, const char *method, const char *path,
  const lc_private_http_header_t *headers,
  const void *body, size_t body_length,
  void *response, size_t response_capacity, size_t *response_length);

#endif
