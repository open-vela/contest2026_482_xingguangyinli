#!/usr/bin/env bash
set -euo pipefail

adb_bin="local-setup/platform-tools/adb"
evidence_dir="tests/evidence/device"
expected_backend="https://api.example.invalid"
serial="1234"
clock_python="${LC_CLOCK_PYTHON:-python3}"
bounded_python="${LC_BOUNDED_PYTHON:-python3}"
adb_timeout_seconds="${LC_ADB_TIMEOUT_SECONDS:-5}"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  cat <<'EOF'
Usage: check_gemini_runtime.sh [--adb PATH] [--evidence-dir DIR]
                               [--expected-backend URL]

Run a read-only Gemini S1 product-runtime preflight. The script checks one
ready ADB target, required processes, the persisted backend URL, and board
UTC date (within 600 seconds of the host, using Python 3). A clock PASS is
not proof of cold-boot NTP or TLS. A backend URL PASS validates only the
persisted value; this preflight does not assert DNS, TCP, TLS, HTTP or service
reachability. LC_CLOCK_PYTHON may select an existing
Python interpreter. LC_ADB_TIMEOUT_SECONDS sets the per-command ADB deadline
(default 5 seconds), and LC_BOUNDED_PYTHON may select the Python interpreter
for deadline enforcement. Missing clock or deadline checking fails closed. The script does not
push, restart, reboot, remount, erase, configure or set the board time.
EOF
}

while (($#)); do
  case "$1" in
    --adb)
      [[ $# -ge 2 ]] || { echo "ERROR: --adb needs a path" >&2; exit 64; }
      adb_bin="$2"
      shift 2
      ;;
    --evidence-dir)
      [[ $# -ge 2 ]] || { echo "ERROR: --evidence-dir needs a path" >&2; exit 64; }
      evidence_dir="$2"
      shift 2
      ;;
    --expected-backend)
      [[ $# -ge 2 ]] || { echo "ERROR: --expected-backend needs a URL" >&2; exit 64; }
      expected_backend="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "ERROR: unknown argument: $1" >&2
      usage >&2
      exit 64
      ;;
  esac
done

[[ -x "$adb_bin" ]] || { echo "ERROR: ADB is not executable: $adb_bin" >&2; exit 69; }
command -v "$bounded_python" >/dev/null 2>&1 || {
  echo "ERROR: bounded-command Python is unavailable" >&2
  exit 69
}
[[ -f "$script_dir/run_bounded_command.py" ]] || {
  echo "ERROR: bounded-command runner is unavailable" >&2
  exit 69
}

adb_bounded() {
  "$bounded_python" "$script_dir/run_bounded_command.py" \
    --timeout "$adb_timeout_seconds" -- "$adb_bin" "$@"
}

devices="$(adb_bounded devices -l)"
target_count="$(printf '%s\n' "$devices" | awk -v serial="$serial" '$1 == serial && $2 == "device" {count++} END {print count+0}')"
serial_count="$(printf '%s\n' "$devices" | awk -v serial="$serial" '$1 == serial {count++} END {print count+0}')"

if [[ "$target_count" != "1" || "$serial_count" != "1" ]]; then
  echo "ERROR: expected exactly one ready Gemini-S1 with serial $serial" >&2
  printf '%s\n' "$devices" >&2
  exit 1
fi

process_list=''
set +e
process_list="$(adb_bounded -s "$serial" shell ps)"
process_read_status=$?
set -e
if ((process_read_status != 0)); then
  echo 'ERROR: failed to read the Gemini S1 process list' >&2
fi
actual_backend=''
set +e
actual_backend="$(adb_bounded -s "$serial" shell cat /data/living_canvas/backend_url)"
backend_read_status=$?
set -e
if ((backend_read_status != 0)); then
  echo 'ERROR: failed to read the persisted backend URL' >&2
fi
board_date=''
set +e
board_date="$(adb_bounded -s "$serial" shell date -u '+%Y-%m-%dT%H:%M:%SZ')"
date_read_status=$?
set -e
if ((date_read_status != 0)); then
  echo 'ERROR: failed to read the board date' >&2
fi

actual_backend="$(printf '%s' "$actual_backend" | tr -d '\r\n')"
# Command substitution already strips terminal LF. Remove only a terminal CR;
# embedded CR/LF must not be collapsed into an apparently valid UTC timestamp.
board_date="${board_date%$'\r'}"
clock_status='FAIL'
clock_report='{"status":"FAIL","reason":"clock_check_unavailable"}'
if command -v "$clock_python" >/dev/null 2>&1 && [[ -f "$script_dir/check_gemini_clock.py" ]]; then
  if clock_report="$("$clock_python" "$script_dir/check_gemini_clock.py" --board-date "$board_date")"; then
    clock_status='PASS'
  fi
  if [[ -z "$clock_report" ]]; then
    clock_report='{"status":"FAIL","reason":"clock_check_unavailable"}'
  fi
fi

status_for() {
  local needle="$1"
  if grep -Fq -- "$needle" <<<"$process_list"; then
    printf 'PASS'
  else
    printf 'FAIL'
  fi
}

mediad_status="$(status_for 'mediad')"
agent_status="$(status_for 'ai_agent')"
product_status="$(status_for 'living_canvas --product')"
if [[ "$actual_backend" == "$expected_backend" ]]; then
  backend_status='PASS'
else
  backend_status='FAIL'
fi

overall='PASS'
for result in "$mediad_status" "$agent_status" "$product_status" "$backend_status" "$clock_status"; do
  if [[ "$result" != 'PASS' ]]; then
    overall='FAIL'
  fi
done

mkdir -p "$evidence_dir"
timestamp="$(date -u '+%Y%m%dT%H%M%SZ')"
evidence_file="$evidence_dir/gemini-runtime-preflight-$timestamp.txt"

{
  echo 'Living Canvas Gemini-S1 read-only runtime preflight'
  echo "Captured (UTC): $timestamp"
  echo "Target serial: $serial"
  echo "Overall: $overall"
  echo 'Scope: runtime/config only; service reachability is not asserted'
  echo
  echo "mediad: $mediad_status"
  echo "ai_agent: $agent_status"
  echo "living_canvas --product: $product_status"
  echo "backend_url: $backend_status"
  echo 'backend_reachability: NOT_CHECKED'
  echo "expected_backend: $expected_backend"
  echo "actual_backend: $actual_backend"
  echo "board_date: $board_date"
  echo "clock: $clock_status"
  echo "clock_check: $clock_report"
  echo
  echo 'Mutations: NONE'
} >"$evidence_file"

echo "Evidence: $evidence_file"
[[ "$overall" == 'PASS' ]]
