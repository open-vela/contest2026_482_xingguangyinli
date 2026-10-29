#!/usr/bin/env bash
set -euo pipefail

app_dir="$(cd "$(dirname "$0")/../.." && pwd)"
display="$app_dir/src/lc_display.c"
makefile="$app_dir/Makefile"
cmake="$app_dir/CMakeLists.txt"
voice_client="$app_dir/src/lc_voice_client.c"

grep -q '#include "lc_native_voice.h"' "$display"
grep -q '#include "lc_agent.h"' "$display"
grep -q '#include "lc_product_flow.h"' "$display"
grep -q '#include "lc_product_transport.h"' "$display"
grep -q 'lc_native_voice_start' "$display"
grep -q 'lc_native_voice_finish' "$display"
grep -q 'lc_native_voice_speak' "$display"
grep -q 'lc_native_voice_create_default' "$display"
if grep -q 'lc_audio_transport_init_default\|lc_audio_transport_start\|voice_channel_start' "$display"; then
  echo 'product UI must not activate the legacy audio owner' >&2
  exit 1
fi
grep -q 'src/lc_native_voice.c' "$makefile"
grep -q 'src/lc_native_voice.c' "$cmake"
grep -q '点击开始说话' "$display"
grep -q '正在聆听，再点发送' "$display"
grep -q '正在识别语音' "$display"
grep -q '正在思考' "$display"
grep -q '正在回答' "$display"
grep -q 'LV_OBJ_FLAG_CLICKABLE' "$display"
grep -q 'src/lc_audio_transport.c' "$makefile"
grep -q 'src/lc_audio_transport.c' "$cmake"
grep -q 'lc_product_transport_recommend' "$display"
grep -q 'lc_product_transport_confirm' "$display"
grep -q 'lc_product_flow_set_recommendation' "$display"
grep -q 'lc_product_flow_set_handoff' "$display"
grep -q '扫码打开饿了么，手机付款' "$display"
grep -q '规则推荐' "$display"
grep -q 'AI + 规则检查' "$display"
grep -q 'ensure_product_backend_timer' "$display"
grep -q 'Could not start backend poll' "$display"
grep -q 'speaking = begin_product_speech(short_reply)' "$display"
grep -q 'packages/ai_agent/src' "$makefile"
grep -q 'packages/ai_agent/src' "$cmake"
grep -q 'backend->private_http' "$voice_client"
grep -q 'lc_private_http_request' "$voice_client"
grep -q 'lc_strict_https_request' "$voice_client"
if grep -q 'vela_https_request_strict' "$voice_client"; then
  echo 'voice transport must use exact public response framing' >&2
  exit 1
fi
grep -q 'src/lc_private_http.c' "$makefile"
grep -q 'src/lc_private_http.c' "$cmake"
grep -q 'src/lc_strict_https.c' "$makefile"
grep -q 'src/lc_strict_https.c' "$cmake"

strict_https="$app_dir/src/lc_strict_https.c"
grep -q 'CLOCK_MONOTONIC' "$strict_https"
grep -q 'O_NONBLOCK' "$strict_https"
grep -q 'SO_ERROR' "$strict_https"
grep -q 'deadline_remaining' "$strict_https"
grep -q 'strict_tls_bio_send' "$strict_https"
grep -q 'strict_tls_bio_receive' "$strict_https"
grep -q 'CONFIG_NETDB_DNSCLIENT_SEND_TIMEOUT' "$strict_https"
grep -q 'CONFIG_NETDB_DNSCLIENT_RECV_TIMEOUT' "$strict_https"
grep -q 'CONFIG_NETDB_DNSCLIENT_RETRIES' "$strict_https"
grep -q 'hints.ai_family = AF_INET' "$strict_https"
grep -q 'resolve_with_deadline' "$strict_https"
grep -q 'pthread_cond_timedwait' "$strict_https"
grep -q 'pthread_condattr_setclock.*CLOCK_MONOTONIC' "$strict_https"
grep -q 'pthread_attr_setdetachstate' "$strict_https"
grep -q 'PTHREAD_CREATE_DETACHED' "$strict_https"
if grep -q 'pthread_join\|pthread_detach' "$strict_https"; then
  echo 'strict resolver must be detached at creation, never joined past deadline' >&2
  exit 1
fi
if grep -q 'mbedtls_net_connect\|mbedtls_net_send\|mbedtls_net_recv\|mbedtls_ssl_close_notify' "$strict_https"; then
  echo 'strict HTTPS must use one nonblocking monotonic deadline through connect, TLS and cleanup' >&2
  exit 1
fi

choice_handler="$(sed -n '/^static void choice_card_event_cb(lv_event_t \*event)$/,/^static bool create_choice_overlay(/p' "$display")"
printf '%s\n' "$choice_handler" | grep -q 'g_product_flow.state == LC_PRODUCT_ERROR'
printf '%s\n' "$choice_handler" | grep -q 'lc_product_flow_retry(&g_product_flow)'
retry_line="$(printf '%s\n' "$choice_handler" | grep -n 'lc_product_flow_retry(&g_product_flow)' | head -1 | cut -d: -f1)"
tap_line="$(printf '%s\n' "$choice_handler" | grep -n 'result = lc_choice_tap' | head -1 | cut -d: -f1)"
if [ "$retry_line" -ge "$tap_line" ]; then
  echo "retryable SAFE STOP must return to choice before processing the next card tap" >&2
  exit 1
fi

voice_worker="$(sed -n '/^static bool spawn_product_voice_worker(/,/^static void set_product_presence_error(/p' "$display")"
if printf '%s\n' "$voice_worker" | grep -q 'g_product_presence_ready'; then
  echo "voice worker setup must not mutate the independent presence sensor state" >&2
  exit 1
fi

if grep -q 'voice_channel_start();' "$display"; then
  echo "display must use the bounded audio transport, not call raw voice APIs" >&2
  exit 1
fi

if grep -q 'static void finish_product_request' "$display"; then
  echo "display must not retain the unused legacy product request helper" >&2
  exit 1
fi

if ! grep -q 'g_product_transcript\[LC_PRODUCT_TRANSCRIPT_BYTES\]' "$display" ||
   ! grep -q 'g_product_backend_transcript\[LC_PRODUCT_TRANSCRIPT_BYTES\]' "$display" ||
   ! grep -q 'char transcript\[LC_PRODUCT_TRANSCRIPT_BYTES\]' "$display"; then
  echo 'product transcript storage must preserve the full ASR UTF8 limit' >&2
  exit 1
fi
recommend_worker="$(sed -n '/^static void \*product_recommend_worker(/,/^}/p' "$display")"
if ! printf '%s\n' "$recommend_worker" | grep -q 'lc_product_transport_recommend_session'; then
  echo 'native recommendation worker must reuse a supplied ASR session' >&2
  exit 1
fi
grep -q 'static bool begin_product_request_with_session' "$display"
grep -q 'memcpy(g_product_session, session_snapshot' "$display"
grep -q 'begin_product_request_with_session(choice, transcript, session)' "$display"
grep -q 'lc_native_voice_current(g_product_audio, g_product_capture_ticket)' "$display"
grep -q '语音未配置，触控可用' "$display"
grep -q 'lc_product_intent_route' "$display"
grep -q 'action == LC_PRODUCT_VOICE_CONFIRM' "$display"
grep -q 'action == LC_PRODUCT_VOICE_REJECT' "$display"
grep -q 'lc_product_transport_reject_session' "$display"
grep -q 'g_product_backend_reject = reject_current' "$display"

echo "product voice source contract passed"
