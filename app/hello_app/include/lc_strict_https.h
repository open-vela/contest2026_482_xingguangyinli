#ifndef LIVING_CANVAS_LC_STRICT_HTTPS_H
#define LIVING_CANVAS_LC_STRICT_HTTPS_H

#include "lc_private_http.h"

#include <stddef.h>

/* Verified TLS plus exact Content-Length HTTP/1.1 framing. This deliberately
 * rejects chunked, missing/duplicate lengths, early EOF, trailing bytes and
 * redirects. It never falls back to plaintext. */
int lc_strict_https_request(
  const char *host, const char *port, const char *method, const char *path,
  const lc_private_http_header_t *headers,
  const void *body, size_t body_length,
  const char *ca_pem, size_t ca_pem_length,
  void *response, size_t response_capacity, size_t *response_length);

#endif
