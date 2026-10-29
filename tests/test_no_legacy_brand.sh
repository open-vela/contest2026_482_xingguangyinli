#!/usr/bin/env bash
set -euo pipefail

denied_lower="$(printf '\163\151\144')"
denied_upper="$(printf '\123\111\104')"
pattern="(^|[^[:alpha:]])${denied_upper}([^[:alpha:]]|$)|lc_${denied_lower}_|LC_${denied_upper}_"

if git grep -n -E "$pattern" -- . ':!tests/test_no_legacy_brand.sh'; then
  echo "FAIL: legacy project branding remains in tracked files" >&2
  exit 1
fi

echo "PASS: repository contains only Living Canvas branding"
