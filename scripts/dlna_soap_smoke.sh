#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LOG_DIR="${PROJECT_ROOT}/logs"
mkdir -p "$LOG_DIR"
LOG_FILE="${LOG_DIR}/$(basename "${BASH_SOURCE[0]}" .sh)-$(date +%Y%m%d-%H%M%S).log"
exec > >(tee -a "$LOG_FILE") 2>&1

HOST="${HOST:-192.168.1.7}"
PORT="${PORT:-49152}"
BASE_URL="http://${HOST}:${PORT}"

INSTANCE_ID="${INSTANCE_ID:-0}"
CHANNEL="${CHANNEL:-Master}"
MEDIA_URI="${MEDIA_URI:-http://192.168.1.40:8000/sample-5s-faststart.mp4}"
SEEK_TARGET="${SEEK_TARGET:-00:00:05}"
STATE_WAIT_SECONDS="${STATE_WAIT_SECONDS:-15}"
HLS_STATE_WAIT_SECONDS="${HLS_STATE_WAIT_SECONDS:-30}"
STATE_POLL_INTERVAL="${STATE_POLL_INTERVAL:-1}"
HLS_READY_GRACE_RETRIES="${HLS_READY_GRACE_RETRIES:-5}"
HLS_READY_GRACE_INTERVAL="${HLS_READY_GRACE_INTERVAL:-0.2}"

LAST_BODY=""
LAST_CODE=""

log() {
  printf '[soap-smoke] %s\n' "$*"
}

trap 'printf "[soap-smoke] log saved: %s\n" "$LOG_FILE"' EXIT

fail() {
  printf '[soap-smoke] ERROR: %s\n' "$*" >&2
  exit 1
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"
}

soap_call() {
  local service="$1"
  local action="$2"
  local args_xml="$3"
  local envelope
  local raw

  envelope="<?xml version=\"1.0\" encoding=\"utf-8\"?>"
  envelope+="<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
  envelope+="<s:Body>"
  envelope+="<u:${action} xmlns:u=\"urn:schemas-upnp-org:service:${service}:1\">${args_xml}</u:${action}>"
  envelope+="</s:Body>"
  envelope+="</s:Envelope>"

  if ! raw="$(curl -sS \
    -X POST "${BASE_URL}/upnp/control/${service}" \
    -H 'Content-Type: text/xml; charset="utf-8"' \
    -H "SOAPACTION: \"urn:schemas-upnp-org:service:${service}:1#${action}\"" \
    --data "${envelope}" \
    -w $'\n%{http_code}')"; then
    fail "curl request failed: service=${service} action=${action}"
  fi

  LAST_CODE="$(printf '%s' "$raw" | tail -n1 | tr -d '\r')"
  LAST_BODY="$(printf '%s' "$raw" | sed '$d')"
}

assert_http_200() {
  [[ "$LAST_CODE" == "200" ]] || fail "expected HTTP 200, got ${LAST_CODE}"
}

assert_body_contains() {
  local needle="$1"
  printf '%s' "$LAST_BODY" | grep -Fq "$needle" || fail "response does not contain: $needle"
}

should_skip_seek() {
  case "${SEEK_TARGET}" in
    ""|"00:00:00"|"00:00:00.0"|"00:00:00.00"|"00:00:00.000")
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

is_hls_uri() {
  case "${MEDIA_URI}" in
    *.m3u8|*.m3u8\?*|*".m3u8&"*|*"format=m3u8"*)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

extract_xml_value() {
  local xml="$1"
  local tag="$2"
  printf '%s' "$xml" | sed -n "s:.*<${tag}>\\([^<]*\\)</${tag}>.*:\\1:p" | head -n1
}

effective_wait_seconds() {
  if is_hls_uri; then
    printf '%s' "$HLS_STATE_WAIT_SECONDS"
  else
    printf '%s' "$STATE_WAIT_SECONDS"
  fi
}

is_nonzero_hhmmss() {
  local value="$1"
  case "$value" in
    ""|"00:00:00"|"00:00:00.0"|"00:00:00.00"|"00:00:00.000")
      return 1
      ;;
    *)
      return 0
      ;;
  esac
}

hls_transport_grace_check() {
  local expected_state="$1"
  local current_state=""

  if [[ "$expected_state" != "PLAYING" ]]; then
    return 1
  fi

  for ((grace = 1; grace <= HLS_READY_GRACE_RETRIES; grace++)); do
    sleep "$HLS_READY_GRACE_INTERVAL"
    soap_call "AVTransport" "GetTransportInfo" "<InstanceID>${INSTANCE_ID}</InstanceID>"
    assert_http_200
    current_state="$(extract_xml_value "$LAST_BODY" "CurrentTransportState")"
    log "observe grace=${grace}/${HLS_READY_GRACE_RETRIES} state=${current_state:-<empty>}"
    if [[ "$current_state" == "$expected_state" ]]; then
      return 0
    fi
  done

  return 1
}

wait_for_transport_state() {
  local expected_state="$1"
  local attempts
  local wait_seconds
  local current_state=""
  local last_state=""
  local position_body=""
  local rel_time=""
  local track_duration=""

  wait_seconds="$(effective_wait_seconds)"
  attempts=$((wait_seconds / STATE_POLL_INTERVAL))
  if [ "$attempts" -le 0 ]; then
    attempts=1
  fi

  for ((i = 1; i <= attempts; i++)); do
    if is_hls_uri; then
      soap_call "AVTransport" "GetPositionInfo" "<InstanceID>${INSTANCE_ID}</InstanceID>"
      if [[ "$LAST_CODE" == "200" ]]; then
        position_body="$LAST_BODY"
        rel_time="$(extract_xml_value "$position_body" "RelTime")"
        track_duration="$(extract_xml_value "$position_body" "TrackDuration")"
      else
        rel_time=""
        track_duration=""
      fi

      soap_call "AVTransport" "GetTransportInfo" "<InstanceID>${INSTANCE_ID}</InstanceID>"
      assert_http_200
      current_state="$(extract_xml_value "$LAST_BODY" "CurrentTransportState")"

      log "observe wait=${i}/${attempts} state=${current_state:-<empty>} rel=${rel_time:-<unknown>} dur=${track_duration:-<unknown>}"

      if [[ "$current_state" == "$expected_state" ]]; then
        return 0
      fi

      if is_nonzero_hhmmss "$track_duration"; then
        if hls_transport_grace_check "$expected_state"; then
          return 0
        fi
      fi
    else
      soap_call "AVTransport" "GetTransportInfo" "<InstanceID>${INSTANCE_ID}</InstanceID>"
      assert_http_200
      current_state="$(extract_xml_value "$LAST_BODY" "CurrentTransportState")"
      if [[ "$current_state" == "$expected_state" ]]; then
        return 0
      fi
      if [[ "$current_state" != "$last_state" ]]; then
        log "observe wait=${i}/${attempts} state=${current_state:-<empty>}"
        last_state="$current_state"
      fi
    fi
    sleep "$STATE_POLL_INTERVAL"
  done

  fail "transport state did not reach ${expected_state} within ${wait_seconds}s"
}

main() {
  log "log file: ${LOG_FILE}"
  require_cmd curl
  require_cmd grep
  require_cmd sed
  require_cmd tail

  log "target: ${BASE_URL}"

  log "1/6 SetAVTransportURI"
  soap_call "AVTransport" "SetAVTransportURI" \
    "<InstanceID>${INSTANCE_ID}</InstanceID><CurrentURI>${MEDIA_URI}</CurrentURI><CurrentURIMetaData></CurrentURIMetaData>"
  assert_http_200

  log "2/6 Play"
  soap_call "AVTransport" "Play" \
    "<InstanceID>${INSTANCE_ID}</InstanceID><Speed>1</Speed>"
  assert_http_200

  log "check transport state == PLAYING"
  wait_for_transport_state "PLAYING"

  sleep 1

  log "3/6 GetPositionInfo"
  soap_call "AVTransport" "GetPositionInfo" \
    "<InstanceID>${INSTANCE_ID}</InstanceID>"
  assert_http_200
  assert_body_contains "<RelTime>"

  if should_skip_seek; then
    log "4/6 Seek skipped target=${SEEK_TARGET:-<empty>}"
  else
    log "4/6 Seek to ${SEEK_TARGET}"
    soap_call "AVTransport" "Seek" \
      "<InstanceID>${INSTANCE_ID}</InstanceID><Unit>REL_TIME</Unit><Target>${SEEK_TARGET}</Target>"
    assert_http_200
  fi

  log "5/6 Pause"
  soap_call "AVTransport" "Pause" \
    "<InstanceID>${INSTANCE_ID}</InstanceID>"
  assert_http_200

  log "check transport state == PAUSED_PLAYBACK"
  wait_for_transport_state "PAUSED_PLAYBACK"

  log "6/6 Stop"
  soap_call "AVTransport" "Stop" \
    "<InstanceID>${INSTANCE_ID}</InstanceID>"
  assert_http_200

  log "check transport state == STOPPED"
  wait_for_transport_state "STOPPED"

  log "optional: GetVolume"
  soap_call "RenderingControl" "GetVolume" \
    "<InstanceID>${INSTANCE_ID}</InstanceID><Channel>${CHANNEL}</Channel>"
  assert_http_200
  assert_body_contains "<CurrentVolume>"

  log "PASS"
}

main "$@"
