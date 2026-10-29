#!/usr/bin/env bash
set -euo pipefail

SCRIPT_UNDER_TEST="${SCRIPT_UNDER_TEST:-$(cd "$(dirname "$0")/.." && pwd)/check_gemini_runtime.sh}"
TEST_ROOT="$(mktemp -d)"
trap 'rm -rf "$TEST_ROOT"' EXIT

cat >"$TEST_ROOT/adb" <<'SH'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$ADB_CALL_LOG"
case "$*" in
  'devices -l')
    printf 'List of devices attached\n1234 device usb:1 product:adb model:adb_board device:NuttX\n'
    ;;
  '-s 1234 shell ps')
    cat <<'EOF'
  PID GROUP PRI POLICY   TYPE    NPX STATE    EVENT     SIGMASK   STACK  USED FILLED COMMAND
  101 0     100 FIFO     Task      0 Waiting  Semaphore 00000000  4096  1024 25.0% mediad
  202 0     100 FIFO     Task      0 Waiting  Semaphore 00000000  8192  2048 25.0% ai_agent
  303 0     100 FIFO     Task      0 Waiting  Semaphore 00000000 16384  4096 25.0% living_canvas --product
EOF
    ;;
  '-s 1234 shell cat /data/living_canvas/backend_url')
    printf 'https://api.example.invalid\n'
    ;;
  '-s 1234 shell date -u +%Y-%m-%dT%H:%M:%SZ')
    printf '%s\n' "$FAKE_BOARD_DATE"
    ;;
  *)
    echo "unexpected adb call: $*" >&2
    exit 90
    ;;
esac
SH
chmod +x "$TEST_ROOT/adb"

export ADB_CALL_LOG="$TEST_ROOT/adb-calls"
export FAKE_BOARD_DATE="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
output="$($SCRIPT_UNDER_TEST \
  --adb "$TEST_ROOT/adb" \
  --evidence-dir "$TEST_ROOT/evidence")"
evidence_file="$(printf '%s\n' "$output" | sed -n 's/^Evidence: //p')"

[[ -f "$evidence_file" ]]
grep -q '^Overall: PASS$' "$evidence_file"
grep -q '^mediad: PASS$' "$evidence_file"
grep -q '^ai_agent: PASS$' "$evidence_file"
grep -q '^living_canvas --product: PASS$' "$evidence_file"
grep -q '^backend_url: PASS$' "$evidence_file"
grep -q '^backend_reachability: NOT_CHECKED$' "$evidence_file"
grep -q '^Scope: runtime/config only; service reachability is not asserted$' "$evidence_file"
grep -Fqx "board_date: $FAKE_BOARD_DATE" "$evidence_file"
grep -q '^clock: PASS$' "$evidence_file"

# The preflight is deliberately read-only and must never push, reboot, remount,
# install, erase, or open an interactive shell.
if grep -Eq '(^| )(push|reboot|remount|install|root|disable-verity)( |$)' "$ADB_CALL_LOG"; then
  echo 'FAIL: runtime preflight attempted a write-capable ADB operation' >&2
  exit 1
fi

cat >"$TEST_ROOT/adb-bad" <<'SH'
#!/usr/bin/env bash
case "$*" in
  'devices -l') printf 'List of devices attached\n1234 device\n' ;;
  '-s 1234 shell ps') printf '1 root living_canvas --product\n' ;;
  '-s 1234 shell cat /data/living_canvas/backend_url') printf 'http://wrong.example\n' ;;
  '-s 1234 shell date -u +%Y-%m-%dT%H:%M:%SZ') printf '%s\n' "$FAKE_BOARD_DATE" ;;
  *) exit 90 ;;
esac
SH
chmod +x "$TEST_ROOT/adb-bad"

if "$SCRIPT_UNDER_TEST" --adb "$TEST_ROOT/adb-bad" --evidence-dir "$TEST_ROOT/bad-evidence" >/dev/null 2>&1; then
  echo 'FAIL: missing required processes or wrong backend URL must fail' >&2
  exit 1
fi

echo 'PASS: check_gemini_runtime'
