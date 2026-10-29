#ifndef LC_HOST_VELA_TLS_H
#define LC_HOST_VELA_TLS_H
#include <stddef.h>
/* SDK HTTP boundary only, matching the actual vela_tls.h declarations. */
typedef struct { const char *name; const char *value; } vela_header_t;
int vela_https_request_strict(const char *host, const char *port,
  const char *method, const char *path, const vela_header_t *headers,
  const char *body, size_t body_length, const char *ca_pem, size_t ca_length,
  char *response, size_t capacity, size_t *received);
#endif
