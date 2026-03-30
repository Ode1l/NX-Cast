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
STATE_POLL_INTERVAL="${STATE_POLL_INTERVAL:-1}"

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

wait_for_transport_state() {
  local expected_state="$1"
  local attempts

  attempts=$((STATE_WAIT_SECONDS / STATE_POLL_INTERVAL))
  if [ "$attempts" -le 0 ]; then
    attempts=1
  fi

  for ((i = 1; i <= attempts; i++)); do
    soap_call "AVTransport" "GetTransportInfo" "<InstanceID>${INSTANCE_ID}</InstanceID>"
    assert_http_200
    if printf '%s' "$LAST_BODY" | grep -Fq "<CurrentTransportState>${expected_state}</CurrentTransportState>"; then
      return 0
    fi
    sleep "$STATE_POLL_INTERVAL"
  done

  fail "transport state did not reach ${expected_state} within ${STATE_WAIT_SECONDS}s"
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

  log "4/6 Seek to ${SEEK_TARGET}"
  soap_call "AVTransport" "Seek" \
    "<InstanceID>${INSTANCE_ID}</InstanceID><Unit>REL_TIME</Unit><Target>${SEEK_TARGET}</Target>"
  assert_http_200

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
