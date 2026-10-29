#!/usr/bin/env bash
set -euo pipefail

app_dir="${APP_DIR:-$(cd "$(dirname "$0")/../.." && pwd)}"

grep -q 'src/lc_ui.c' "$app_dir/Makefile"
grep -q 'src/lc_ui.c' "$app_dir/CMakeLists.txt"
grep -q 'src/lc_touch.c' "$app_dir/Makefile"
grep -q 'src/lc_touch.c' "$app_dir/CMakeLists.txt"
grep -q 'src/lc_checkin.c' "$app_dir/Makefile"
grep -q 'src/lc_checkin.c' "$app_dir/CMakeLists.txt"
grep -q 'src/generated/lc_choice_assets.c' "$app_dir/Makefile"
grep -q 'src/generated/lc_choice_assets.c' "$app_dir/CMakeLists.txt"
grep -q 'lc_display_run_image_preview' "$app_dir/include/lc_display.h"
grep -q 'lc_display_run_choice_preview' "$app_dir/include/lc_display.h"
grep -q 'lc_display_run_competition_demo' "$app_dir/include/lc_display.h"
grep -q 'lc_display_run_project_demo' "$app_dir/include/lc_display.h"
grep -q 'lc_display_run_product_accessory' "$app_dir/include/lc_display.h"
grep -q 'product_presence_worker' "$app_dir/src/lc_display.c"
grep -q 'lc_accessory_runtime_feed' "$app_dir/src/lc_display.c"
grep -q '感应设备未连接' "$app_dir/src/lc_display.c"
grep -q 'create_greeting_overlay(screen, false)' "$app_dir/src/lc_display.c"
grep -q '在手机查看项目' "$app_dir/src/lc_display.c"
grep -q '公开项目链接' "$app_dir/src/lc_display.c"
grep -q 'LC_COMPETITION_REQUESTING' "$app_dir/src/lc_display.c"
grep -q 'LC_COMPETITION_RECOMMENDATION' "$app_dir/src/lc_display.c"
grep -q 'LC_COMPETITION_CONFIRMING' "$app_dir/src/lc_display.c"
grep -q 'LC_COMPETITION_QR' "$app_dir/src/lc_display.c"
grep -q 'LC_COMPETITION_ERROR' "$app_dir/src/lc_display.c"
grep -q 'select LV_USE_QRCODE' "$app_dir/Kconfig"
grep -q 'lv_qrcode_create' "$app_dir/src/lc_display.c"
grep -q 'lv_qrcode_update' "$app_dir/src/lc_display.c"
grep -q 'g_competition.dish' "$app_dir/src/lc_display.c"
grep -q 'g_competition.reason' "$app_dir/src/lc_display.c"
grep -q 'g_competition.price' "$app_dir/src/lc_display.c"
grep -q 'LV_EVENT_CLICKED' "$app_dir/src/lc_display.c"
grep -q 'lc_choice_tap' "$app_dir/src/lc_display.c"
grep -q 'lc_touch_transform' "$app_dir/src/lc_display.c"
grep -q 'lv_indev_set_read_cb' "$app_dir/src/lc_display.c"
grep -q 'lc_ui_compute_choice_layout' "$app_dir/src/lc_display.c"
grep -q 'lv_image_set_scale' "$app_dir/src/lc_display.c"
grep -q 'interactive preview requires a touch input device' \
  "$app_dir/src/lc_display.c"
if grep -q 'TOMATO BEEF RICE' "$app_dir/src/lc_display.c"; then
  echo 'FAIL: competition renderer still hard-codes the recommended dish' >&2
  exit 1
fi
grep -q '_Static_assert(sizeof(lc_bg_portrait)' \
  "$app_dir/src/generated/lc_choice_assets.c"

echo 'PASS: lc_ui build metadata'
