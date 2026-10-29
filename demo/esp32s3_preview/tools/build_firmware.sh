#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
arduino_cli="${ARDUINO_CLI:-arduino-cli}"
build_root="${LC_BUILD_ROOT:-$project_dir/build}"
private_config="${LC_PRIVATE_CONFIG_HEADER:-}"
fqbn="esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=custom,PSRAM=opi"

mkdir -p "$build_root/arduino" "$build_root/firmware"

# Arduino CLI requires the sketch directory basename to match its primary .ino
# file. Keep the repository's short directory name, but compile an exact,
# disposable source-only staging copy under the required project name.
stage_root=$(mktemp -d /tmp/lc-esp32-build.XXXXXX)
stage_dir="$stage_root/living_canvas_esp32s3_preview"
cleanup() {
  case "$stage_root" in
    /tmp/lc-esp32-build.*) rm -rf -- "$stage_root" ;;
  esac
}
trap cleanup EXIT HUP INT TERM
mkdir -p "$stage_dir"
cp "$project_dir/living_canvas_esp32s3_preview.ino" \
  "$stage_dir/living_canvas_esp32s3_preview.ino"
cp "$project_dir/board_pins.h" "$project_dir/partitions.csv" "$stage_dir/"
cp -R "$project_dir/generated" "$project_dir/src" "$stage_dir/"
# The repository-level generated directory may contain an ignored private
# device header from an earlier A/B build. Generic preview builds must not
# inherit it implicitly; only the explicit path below may opt in.
rm -f -- "$stage_dir/generated/living_canvas_private_config.h"
if [ -n "$private_config" ]; then
  test -f "$private_config" || {
    echo "Missing explicit private config header" >&2
    exit 2
  }
  cp "$private_config" "$stage_dir/generated/living_canvas_private_config.h"
fi

"$arduino_cli" compile \
  --fqbn "$fqbn" \
  --build-path "$build_root/arduino" \
  --output-dir "$build_root/firmware" \
  "$stage_dir"

if [ "$build_root" = "$project_dir/build" ]; then
  (
    cd "$project_dir"
    shasum -a 256 \
      build/firmware/living_canvas_esp32s3_preview.ino.bootloader.bin \
      build/firmware/living_canvas_esp32s3_preview.ino.partitions.bin \
      build/arduino/boot_app0.bin \
      build/firmware/living_canvas_esp32s3_preview.ino.bin \
      > build/checksums.sha256
  )
else
  (
    cd "$build_root"
    shasum -a 256 \
      firmware/living_canvas_esp32s3_preview.ino.bootloader.bin \
      firmware/living_canvas_esp32s3_preview.ino.partitions.bin \
      arduino/boot_app0.bin \
      firmware/living_canvas_esp32s3_preview.ino.bin \
      > checksums.sha256
  )
fi
