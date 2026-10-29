#!/usr/bin/env bash
set -euo pipefail

app_dir="${APP_DIR:-$(cd "$(dirname "$0")/../.." && pwd)}"
symbol='LVX_USE_DEMO_CONTEST2026_482_LIVING_CANVAS'

grep -q "config $symbol" "$app_dir/Kconfig"
grep -q "CONFIG_$symbol" "$app_dir/Make.defs"
grep -q 'packages/demos/contest2026_482_hello_app' "$app_dir/Make.defs"
grep -q "CONFIG_$symbol" "$app_dir/Makefile"
grep -q "CONFIG_$symbol" "$app_dir/CMakeLists.txt"

for source in lc_state.c lc_clock.c lc_dinner.c lc_memory.c lc_voice.c lc_agent.c lc_agent_bridge.c lc_display.c; do
  grep -q "src/$source" "$app_dir/Makefile"
  grep -q "src/$source" "$app_dir/CMakeLists.txt"
done

grep -q 'include' "$app_dir/Makefile"
grep -q 'INCLUDE_DIRECTORIES' "$app_dir/CMakeLists.txt"
grep -q 'packages/ai_agent/include' "$app_dir/Makefile"
grep -q 'packages/ai_agent/include' "$app_dir/CMakeLists.txt"
grep -q '#include "lc_voice.h"' "$app_dir/hello_app_main.c"
grep -q 'lc_voice_init' "$app_dir/hello_app_main.c"
grep -q '#include "lc_agent.h"' "$app_dir/hello_app_main.c"
grep -q '#include "lc_agent_bridge.h"' "$app_dir/hello_app_main.c"
grep -q -- '--agent-prompt' "$app_dir/hello_app_main.c"
grep -q '#include "lc_display.h"' "$app_dir/hello_app_main.c"
grep -q -- '--ui-preview' "$app_dir/hello_app_main.c"
grep -q 'DEPENDS' "$app_dir/CMakeLists.txt"
grep -q 'lvgl' "$app_dir/CMakeLists.txt"
grep -q 'lv_nuttx_init' "$app_dir/src/lc_display.c"
grep -q '"/dev/lcd0"' "$app_dir/src/lc_display.c"
grep -q 'int lc_display_run_preview(void)' "$app_dir/src/lc_display.c"

cmake_sources="$(grep -Eo 'src/[A-Za-z0-9_./-]+\.c' \
  "$app_dir/CMakeLists.txt" | sort -u)"
make_sources="$(grep -Eo 'src/[A-Za-z0-9_./-]+\.c' \
  "$app_dir/Makefile" | sort -u)"
if [[ "$cmake_sources" != "$make_sources" ]]; then
  echo 'FAIL: CMakeLists.txt and Makefile source manifests differ' >&2
  diff -u <(printf '%s\n' "$cmake_sources") \
    <(printf '%s\n' "$make_sources") >&2 || true
  exit 1
fi
while IFS= read -r source; do
  [[ -n "$source" ]] || continue
  test -f "$app_dir/$source" || {
    echo "FAIL: build manifest references missing source: $source" >&2
    exit 1
  }
done <<<"$cmake_sources"


if grep -Eq 'CONTEST2026_000|team 000' \
  "$app_dir/Kconfig" "$app_dir/Make.defs" "$app_dir/Makefile" \
  "$app_dir/CMakeLists.txt" "$app_dir/hello_app_main.c"; then
  echo 'FAIL: template team 000 metadata remains' >&2
  exit 1
fi

echo 'PASS: openvela build metadata'
