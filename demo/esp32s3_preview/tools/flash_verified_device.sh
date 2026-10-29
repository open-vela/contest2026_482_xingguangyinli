#!/bin/sh
set -eu
umask 077

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
esptool="${ESPTOOL_BIN:-esptool}"
python_bin="${PYTHON_BIN:-python3}"
identity_tool="${ESP32_PORT_IDENTITY_TOOL:-$project_dir/tools/verify_port_identity.py}"
binding_tool="${ESP32_DEVICE_BINDING_TOOL:-$project_dir/tools/device_binding.py}"
delivery_verifier="${ESP32_DELIVERY_VERIFIER:-$project_dir/tools/verify_device_delivery.py}"
binding_registry="${LC_DEVICE_BINDING_REGISTRY:-$project_dir/build/device-bindings.json}"
backup_root="${LC_ESP32_BACKUP_ROOT:-$project_dir/build/device-backups}"
expected_mac="${EXPECTED_ESP32_MAC:-}"
expected_usb_serial="${EXPECTED_ESP32_USB_SERIAL:-}"
expected_device_id="${LC_EXPECTED_DEVICE_ID:-}"
device_build_dir="${LC_DEVICE_BUILD_DIR:-}"
operator_confirmation="${LC_OPERATOR_CONFIRMATION:-}"

test "$operator_confirmation" = "人在板旁、供电稳定、线缆不动" || {
  echo "Fresh operator confirmation is required before ESP32 device access." >&2
  exit 2
}

test -n "$expected_mac" || {
  echo "Set EXPECTED_ESP32_MAC to the target board MAC before flashing." >&2
  exit 2
}
printf '%s\n' "$expected_mac" | grep -Eq '^[[:xdigit:]]{2}(:[[:xdigit:]]{2}){5}$' || {
  echo "EXPECTED_ESP32_MAC must be a six-byte colon-separated MAC." >&2
  exit 2
}
test -n "$expected_usb_serial" || {
  echo "Set EXPECTED_ESP32_USB_SERIAL before flashing." >&2
  exit 2
}
test -f "$identity_tool" || {
  echo "ESP32 USB identity verifier is unavailable." >&2
  exit 2
}
test -f "$binding_tool" || {
  echo "ESP32 device binding verifier is unavailable." >&2
  exit 2
}
test -f "$delivery_verifier" || {
  echo "ESP32 delivery verifier is unavailable." >&2
  exit 2
}
case "$expected_device_id" in
  cat_a|cat_b) ;;
  *)
    echo "Set LC_EXPECTED_DEVICE_ID to cat_a or cat_b before flashing." >&2
    exit 2
    ;;
esac
test -d "$device_build_dir" || {
  echo "Set LC_DEVICE_BUILD_DIR to one verified device delivery." >&2
  exit 2
}
test -f "$device_build_dir/identity.txt" || {
  echo "Device delivery has no identity file." >&2
  exit 2
}
actual_device_id=$(sed -n '1p' "$device_build_dir/identity.txt")
test "$actual_device_id" = "$expected_device_id" || {
  echo "Refusing to flash: delivery identity does not match target identity." >&2
  exit 3
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

"$python_bin" "$delivery_verifier" "$device_build_dir"

mac_output=$("$esptool" --chip esp32s3 --port "$port" --baud 115200 --no-stub read_mac)
actual_mac=$(printf '%s\n' "$mac_output" |
  sed -n 's/^MAC:[[:space:]]*//p' |
  tr '[:upper:]' '[:lower:]' |
  sort -u)
printf '%s\n' "$actual_mac" | grep -Eq '^[[:xdigit:]]{2}(:[[:xdigit:]]{2}){5}$' || {
  echo "Refusing to flash: esptool returned an invalid or ambiguous MAC." >&2
  exit 3
}
expected_mac_lower=$(printf '%s' "$expected_mac" | tr '[:upper:]' '[:lower:]')
test "$actual_mac" = "$expected_mac_lower" || {
  echo "Refusing to flash: target MAC is not $expected_mac" >&2
  exit 3
}
printf '%s\n' "ESP32_CHIP_MAC_OK"

flash_id_output=$("$esptool" --chip esp32s3 --port "$port" --baud 115200 --no-stub flash_id)
printf '%s\n' "$flash_id_output" | tr -d '\r' | \
  grep -Fxq 'Detected flash size: 16MB' || {
  echo "Refusing to flash: target is not a verified 16MB device." >&2
  exit 3
}
printf '%s\n' "ESP32_FLASH_SIZE_OK"

"$python_bin" "$binding_tool" record \
  --registry "$binding_registry" \
  --device-id "$expected_device_id" \
  --usb-serial "$expected_usb_serial" \
  --mac "$actual_mac"

test ! -L "$backup_root" || {
  echo "Refusing to use a symlinked ESP32 backup root." >&2
  exit 2
}
mkdir -p "$backup_root"
backup_dir="$backup_root/$expected_device_id"
test ! -L "$backup_dir" || {
  echo "Refusing to use a symlinked ESP32 backup directory." >&2
  exit 2
}
mkdir -p "$backup_dir"
chmod 700 "$backup_root" "$backup_dir"
stamp=$(date -u +%Y%m%dT%H%M%SZ)-$$
mac_slug=$(printf '%s' "$actual_mac" | tr ':' '-')
backup_incoming="$backup_dir/.incoming-$stamp-$mac_slug.bin"
backup_file="$backup_dir/$stamp-$mac_slug.bin"
cleanup_backup() {
  case "${backup_incoming:-}" in
    "$backup_dir"/.incoming-*.bin) rm -f -- "$backup_incoming" ;;
  esac
}
trap cleanup_backup EXIT HUP INT TERM

"$esptool" --chip esp32s3 --port "$port" --baud 115200 --no-stub read_flash \
  0x0 0x1000000 "$backup_incoming"
test -s "$backup_incoming" || {
  echo "Refusing to flash: ESP32 rollback backup is empty." >&2
  exit 3
}
backup_size=$(wc -c < "$backup_incoming" | tr -d '[:space:]')
test "$backup_size" = "16777216" || {
  echo "Refusing to flash: ESP32 rollback backup is not exactly 16MiB." >&2
  exit 3
}
chmod 600 "$backup_incoming"
backup_hash=$(shasum -a 256 "$backup_incoming" | awk '{print $1}')
printf '%s\n' "$backup_hash" | grep -Eq '^[[:xdigit:]]{64}$' || {
  echo "Refusing to flash: ESP32 rollback backup hash is invalid." >&2
  exit 3
}
mv "$backup_incoming" "$backup_file"
backup_incoming=
printf '%s  %s\n' "$backup_hash" "$(basename "$backup_file")" \
  > "$backup_file.sha256"
chmod 600 "$backup_file.sha256"
printf '%s\n' "ESP32_FLASH_BACKUP_OK"

"$esptool" --chip esp32s3 --port "$port" --baud 115200 --no-stub write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 "$device_build_dir/bootloader.bin" \
  0x8000 "$device_build_dir/partitions.bin" \
  0xe000 "$device_build_dir/boot_app0.bin" \
  0x10000 "$device_build_dir/application.bin"

"$esptool" --chip esp32s3 --port "$port" --baud 115200 --no-stub verify_flash \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 "$device_build_dir/bootloader.bin" \
  0x8000 "$device_build_dir/partitions.bin" \
  0xe000 "$device_build_dir/boot_app0.bin" \
  0x10000 "$device_build_dir/application.bin"
printf '%s\n' "ESP32_FLASH_VERIFY_OK"
