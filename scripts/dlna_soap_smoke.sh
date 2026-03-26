#!/usr/bin/env bash

set -euo pipefail

HOST="${HOST:-192.168.1.7}"
PORT="${PORT:-49152}"
BASE_URL="http://${HOST}:${PORT}"

INSTANCE_ID="${INSTANCE_ID:-0}"
CHANNEL="${CHANNEL:-Master}"
MEDIA_URI="${MEDIA_URI:-http://example.com/mock-media.mp3}"
SEEK_TARGET="${SEEK_TARGET:-00:00:10}"

LAST_BODY=""
LAST_CODE=""

log() {
  printf '[soap-smoke] %s\n' "$*"
}

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

main() {
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
  soap_call "AVTransport" "GetTransportInfo" \
    "<InstanceID>${INSTANCE_ID}</InstanceID>"
  assert_http_200
  assert_body_contains "<CurrentTransportState>PLAYING</CurrentTransportState>"

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
  soap_call "AVTransport" "GetTransportInfo" \
    "<InstanceID>${INSTANCE_ID}</InstanceID>"
  assert_http_200
  assert_body_contains "<CurrentTransportState>PAUSED_PLAYBACK</CurrentTransportState>"

  log "6/6 Stop"
  soap_call "AVTransport" "Stop" \
    "<InstanceID>${INSTANCE_ID}</InstanceID>"
  assert_http_200

  log "check transport state == STOPPED"
  soap_call "AVTransport" "GetTransportInfo" \
    "<InstanceID>${INSTANCE_ID}</InstanceID>"
  assert_http_200
  assert_body_contains "<CurrentTransportState>STOPPED</CurrentTransportState>"

  log "optional: GetVolume"
  soap_call "RenderingControl" "GetVolume" \
    "<InstanceID>${INSTANCE_ID}</InstanceID><Channel>${CHANNEL}</Channel>"
  assert_http_200
  assert_body_contains "<CurrentVolume>"

  log "PASS"
}

main "$@"
