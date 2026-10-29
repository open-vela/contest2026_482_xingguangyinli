#!/bin/sh
set -eu
umask 077

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
esptool="${ESPTOOL_BIN:-esptool}"
python_bin="${PYTHON_BIN:-python3}"
identity_tool="${ESP32_PORT_IDENTITY_TOOL:-$project_dir/tools/verify_port_identity.py}"
binding_tool="${ESP32_DEVICE_BINDING_TOOL:-$project_dir/tools/device_binding.py}"
binding_registry="${LC_DEVICE_BINDING_REGISTRY:-$project_dir/build/device-bindings.json}"
expected_mac="${EXPECTED_ESP32_MAC:-}"
expected_usb_serial="${EXPECTED_ESP32_USB_SERIAL:-}"
expected_device_id="${LC_EXPECTED_DEVICE_ID:-}"
backup_file="${LC_ESP32_BACKUP_FILE:-}"
operator_confirmation="${LC_OPERATOR_CONFIRMATION:-}"

test "$operator_confirmation" = "人在板旁、供电稳定、线缆不动" || {
  echo "Fresh operator confirmation is required before ESP32 restore." >&2
  exit 2
}
case "$expected_device_id" in
  cat_a|cat_b) ;;
  *)
    echo "Set LC_EXPECTED_DEVICE_ID to cat_a or cat_b before restore." >&2
    exit 2
    ;;
esac
printf '%s\n' "$expected_mac" | grep -Eq '^[[:xdigit:]]{2}(:[[:xdigit:]]{2}){5}$' || {
  echo "EXPECTED_ESP32_MAC must be a six-byte colon-separated MAC." >&2
  exit 2
}
test -n "$expected_usb_serial" || {
  echo "Set EXPECTED_ESP32_USB_SERIAL before restore." >&2
  exit 2
}
test -n "$backup_file" && test -f "$backup_file" && test ! -L "$backup_file" || {
  echo "Set LC_ESP32_BACKUP_FILE to one regular rollback image." >&2
  exit 2
}
backup_dir=$(CDPATH= cd -- "$(dirname -- "$backup_file")" && pwd)
backup_file="$backup_dir/$(basename "$backup_file")"
test "$(basename "$backup_dir")" = "$expected_device_id" || {
  echo "Refusing to restore a backup for the other device identity." >&2
  exit 3
}
checksum_file="$backup_file.sha256"
test -f "$checksum_file" && test ! -L "$checksum_file" || {
  echo "Rollback image checksum is missing or unsafe." >&2
  exit 2
}
"$python_bin" -c '
import os
import stat
import sys

safe = all(
    stat.S_IMODE(os.stat(path, follow_symlinks=False).st_mode) == 0o600
    for path in sys.argv[1:]
)
raise SystemExit(0 if safe else 1)
' "$backup_file" "$checksum_file" || {
  echo "Rollback image and checksum must both have mode 0600." >&2
  exit 2
}
backup_size=$(wc -c < "$backup_file" | tr -d '[:space:]')
test "$backup_size" = "16777216" || {
  echo "Rollback image must be exactly 16 MiB." >&2
  exit 3
}
checksum_lines=$(wc -l < "$checksum_file" | tr -d '[:space:]')
test "$checksum_lines" = "1" || {
  echo "Rollback image checksum record is malformed." >&2
  exit 3
}
backup_hash=$(shasum -a 256 "$backup_file" | awk '{print $1}')
expected_record="$backup_hash  $(basename "$backup_file")"
actual_record=$(cat "$checksum_file")
test "$actual_record" = "$expected_record" || {
  echo "Rollback image checksum does not match." >&2
  exit 3
}

test -f "$identity_tool" && test -f "$binding_tool" || {
  echo "ESP32 restore identity tools are unavailable." >&2
  exit 2
}
"$python_bin" "$binding_tool" check \
  --registry "$binding_registry" \
  --device-id "$expected_device_id" \
  --usb-serial "$expected_usb_serial" \
  --mac "$expected_mac"

if [ -n "${LC_ESP32_PORT:-}" ]; then
  port="$LC_ESP32_PORT"
  test -e "$port" || {
    echo "Explicit ESP32 port does not exist." >&2
    exit 2
  }
else
  set -- /dev/cu.usbmodem*
  if [ "$#" -ne 1 ] || [ ! -e "$1" ]; then
    echo "Expected exactly one /dev/cu.usbmodem device; found $#" >&2
    exit 2
  fi
  port="$1"
fi

"$python_bin" "$identity_tool" \
  --port "$port" \
  --expected-usb-serial "$expected_usb_serial"
mac_output=$("$esptool" --chip esp32s3 --port "$port" --baud 115200 --no-stub read_mac)
actual_mac=$(printf '%s\n' "$mac_output" |
  sed -n 's/^MAC:[[:space:]]*//p' |
  tr '[:upper:]' '[:lower:]' |
  sort -u)
printf '%s\n' "$actual_mac" | grep -Eq '^[[:xdigit:]]{2}(:[[:xdigit:]]{2}){5}$' || {
  echo "Refusing to restore: esptool returned an invalid or ambiguous MAC." >&2
  exit 3
}
expected_mac_lower=$(printf '%s' "$expected_mac" | tr '[:upper:]' '[:lower:]')
test "$actual_mac" = "$expected_mac_lower" || {
  echo "Refusing to restore: target MAC does not match the rollback image." >&2
  exit 3
}
printf '%s\n' "ESP32_RESTORE_MAC_OK"

flash_id_output=$("$esptool" --chip esp32s3 --port "$port" --baud 115200 --no-stub flash_id)
printf '%s\n' "$flash_id_output" | tr -d '\r' | \
  grep -Fxq 'Detected flash size: 16MB' || {
  echo "Refusing to restore: target is not a verified 16MB device." >&2
  exit 3
}
printf '%s\n' "ESP32_RESTORE_FLASH_SIZE_OK"

"$esptool" --chip esp32s3 --port "$port" --baud 115200 --no-stub write_flash \
  --flash_mode keep --flash_freq keep --flash_size keep \
  0x0 "$backup_file"
"$esptool" --chip esp32s3 --port "$port" --baud 115200 --no-stub verify_flash \
  0x0 "$backup_file"
printf '%s\n' "ESP32_RESTORE_VERIFY_OK"
