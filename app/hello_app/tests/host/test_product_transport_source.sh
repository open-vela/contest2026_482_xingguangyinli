#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/../.." && pwd)"
source_file="$root/src/lc_product_transport.c"
header_file="$root/include/lc_product_transport.h"
kconfig_file="$root/Kconfig"

test -f "$source_file"
test -f "$header_file"

grep -q 'lc_strict_https_request' "$source_file"
grep -q 'lc_private_http_request' "$source_file"
if grep -q 'vela_https_request_strict' "$source_file"; then
  echo 'product transport must use exact framing, not the permissive SDK collector' >&2
  exit 1
fi
grep -q '#include "lc_product_trust.h"' "$source_file"
grep -q 'lc_product_trust_anchor_pem' "$source_file"
grep -q 'lc_product_trust_anchor_pem_size' "$source_file"
grep -q '#  include <netutils/cJSON.h>' "$source_file"
grep -q 'select NETUTILS_CJSON' "$kconfig_file"
grep -q 'response_length >= response_capacity - 1u' "$source_file"
grep -q 'cJSON_ParseWithOpts' "$source_file"

if grep -q 'char response\[LC_PRODUCT_RESPONSE_MAX\]' "$source_file"; then
  echo "product transport must not place the full HTTPS response on a worker stack" >&2
  exit 1
fi
grep -q 'cJSON_Parse' "$source_file"
grep -q '"/v1/session"' "$source_file"
grep -q '"/v1/dialogue"' "$source_file"
grep -q '"/v1/device/event"' "$source_file"
grep -q '"/v1/confirm"' "$source_file"
grep -q 'status < 200 || status >= 300' "$source_file"
grep -q 'LC_PRODUCT_TRANSPORT_CONFIG_PATH' "$header_file"
grep -q 'LC_PRODUCT_DEVICE_TOKEN_CONFIG_PATH' "$header_file"
grep -q 'lc_product_transport_init_authenticated' "$source_file"
grep -q 'lc_product_transport_clear' "$source_file"
grep -q '"Authorization"' "$source_file"
grep -q 'transport->authorization' "$source_file"
grep -q 'transport->private_http' "$source_file"

# Plain HTTP is an explicit roadshow fallback only for RFC1918 literals. It
# must never weaken the public hostname path or silently fall back from TLS.
grep -q 'lc_private_http_host_allowed' "$source_file"

# The backend URL may be provisioned after the display process starts.  A
# product request must retry the default transport instead of permanently
# retaining the startup-time "not configured" result.
begin_product_request="$({
  sed -n '/static bool begin_product_request/,/^}/p' "$root/src/lc_display.c"
})"
grep -q 'lc_product_transport_init_default(&g_product_transport)' \
  <<<"$begin_product_request"

if grep -Eq 'system\(|wget_post\(|sk-[A-Za-z0-9]' "$source_file"; then
  echo 'product transport must not shell out or embed provider credentials' >&2
  exit 1
fi

echo 'PASS: product transport source contract'
