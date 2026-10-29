#ifndef LIVING_CANVAS_LC_HTTP_FRAMING_INTERNAL_H
#define LIVING_CANVAS_LC_HTTP_FRAMING_INTERNAL_H

#include "lc_private_http.h"

/* Internal collector shared only by the explicit private transport and the
 * verified-TLS adapter. It is not a public plaintext escape hatch. */
int lc_http_framed_request_with_ops(
  const lc_private_http_ops_t *ops, void *context, bool private_only,
  const char *host, const char *port, const char *method, const char *path,
  const lc_private_http_header_t *headers,
  const void *body, size_t body_length,
  void *response, size_t response_capacity, size_t *response_length);

#endif
