#!/bin/sh
set -eu

umask 077
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
device_id="${LC_CAT_DEVICE_ID:-}"
case "$device_id" in
  cat_a|cat_b) ;;
  *)
    echo "LC_CAT_DEVICE_ID must be cat_a or cat_b" >&2
    exit 2
    ;;
esac

generator="${LC_CONFIG_GENERATOR:-$project_dir/tools/generate_private_config.py}"
builder="${LC_FIRMWARE_BUILDER:-$project_dir/tools/build_firmware.sh}"
python_bin="${PYTHON_BIN:-python3}"
provided_private_config="${LC_PRIVATE_CONFIG_HEADER:-}"
delivery_root="${LC_DEVICE_BUILD_ROOT:-$project_dir/build/devices}"
device_root="$delivery_root/$device_id"
incoming=

stage_root=$(mktemp -d /tmp/lc-esp32-device-build.XXXXXX)
cleanup() {
  case "${incoming:-}" in
    "$device_root"/.incoming-*) rm -rf -- "$incoming" ;;
  esac
  case "$stage_root" in
    /tmp/lc-esp32-device-build.*) rm -rf -- "$stage_root" ;;
  esac
}
trap cleanup EXIT HUP INT TERM

private_config="$stage_root/living_canvas_private_config.h"
if [ -n "$provided_private_config" ]; then
  test -f "$provided_private_config" && test ! -L "$provided_private_config" || {
    echo "Pre-generated private config must be a regular file." >&2
    exit 2
  }
  "$python_bin" "$generator" \
    --verify-header "$provided_private_config" \
    --expected-device-id "$device_id"
  cp "$provided_private_config" "$private_config"
  chmod 600 "$private_config"
else
  export LC_PRIVATE_CONFIG_OUTPUT="$private_config"
  "$python_bin" "$generator"
fi

export LC_PRIVATE_CONFIG_HEADER="$private_config"
export LC_BUILD_ROOT="$stage_root/build"
"$builder"

app_name="living_canvas_esp32s3_preview.ino"
for required in \
  "$LC_BUILD_ROOT/firmware/$app_name.bootloader.bin" \
  "$LC_BUILD_ROOT/firmware/$app_name.partitions.bin" \
  "$LC_BUILD_ROOT/arduino/boot_app0.bin" \
  "$LC_BUILD_ROOT/firmware/$app_name.bin"; do
  test -f "$required" || {
    echo "Device build missing an expected artifact" >&2
    exit 2
  }
done

stamp=$(date -u +%Y%m%dT%H%M%SZ)-$$
incoming="$device_root/.incoming-$stamp"
destination="$device_root/$stamp"
mkdir -p "$incoming"

cp "$LC_BUILD_ROOT/firmware/$app_name.bootloader.bin" "$incoming/bootloader.bin"
cp "$LC_BUILD_ROOT/firmware/$app_name.partitions.bin" "$incoming/partitions.bin"
cp "$LC_BUILD_ROOT/arduino/boot_app0.bin" "$incoming/boot_app0.bin"
cp "$LC_BUILD_ROOT/firmware/$app_name.bin" "$incoming/application.bin"
printf '%s\n' "$device_id" > "$incoming/identity.txt"
(
  cd "$incoming"
  shasum -a 256 \
    bootloader.bin partitions.bin boot_app0.bin application.bin \
    > checksums.sha256
)
mv "$incoming" "$destination"
incoming=
printf 'device firmware created: %s\n' "$destination"
