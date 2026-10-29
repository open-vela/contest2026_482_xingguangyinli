#!/usr/bin/env bash
set -euo pipefail

source_file="$(cd "$(dirname "$0")/../.." && pwd)/src/lc_private_http.c"

grep -q 'O_NONBLOCK' "$source_file"
grep -q 'select(' "$source_file"
grep -q 'SO_ERROR' "$source_file"
grep -q 'LC_PRIVATE_HTTP_CONNECT_TIMEOUT_SECONDS' "$source_file"
grep -q 'LC_PRIVATE_HTTP_TOTAL_TIMEOUT_SECONDS' "$source_file"
grep -q 'CLOCK_MONOTONIC' "$source_file"
grep -q 'deadline_remaining' "$source_file"
grep -q 'wipe_sensitive' "$source_file"

if grep -q '(void)setsockopt' "$source_file"; then
  echo 'private HTTP must not ignore socket timeout installation failures' >&2
  exit 1
fi

if grep -q 'while (amount < 0 && errno == EINTR)' "$source_file"; then
  echo 'private HTTP send/receive must re-check the request deadline after EINTR' >&2
  exit 1
fi

if grep -q 'getaddrinfo' "$source_file"; then
  echo 'private HTTP must never resolve a hostname' >&2
  exit 1
fi

echo 'PASS: private HTTP has bounded numeric-only connect policy'
