#!/usr/bin/env bash

set -euo pipefail

HOST="${HOST:-192.168.1.7}"
PORT="${PORT:-49152}"
BASE_URL="http://${HOST}:${PORT}"

SSDP_PORT="${SSDP_PORT:-1900}"
SSDP_TARGET_IP="${SSDP_TARGET_IP:-239.255.255.250}"
SSDP_HOST_HEADER="${SSDP_HOST_HEADER:-239.255.255.250:1900}"
SSDP_TIMEOUT_SEC="${SSDP_TIMEOUT_SEC:-3}"
VERBOSE="${VERBOSE:-0}"

LAST_BODY=""
LAST_CODE=""

log() {
  printf '[desc-discovery-smoke] %s\n' "$*"
}

fail() {
  printf '[desc-discovery-smoke] ERROR: %s\n' "$*" >&2
  exit 1
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"
}

http_get() {
  local path="$1"
  local raw

  if ! raw="$(curl -sS "${BASE_URL}${path}" -w $'\n%{http_code}')"; then
    fail "curl request failed: GET ${path}"
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

extract_tag_value() {
  local xml="$1"
  local tag="$2"
  printf '%s' "$xml" | tr -d '\r\n' | sed -n "s:.*<${tag}>\\([^<]*\\)</${tag}>.*:\\1:p"
}

ssdp_probe() {
  local st="$1"
  local expected_st="$2"
  local expected_uuid="$3"
  local request
  local response

  request="$(printf 'M-SEARCH * HTTP/1.1\r\nHOST: %s\r\nMAN: "ssdp:discover"\r\nMX: 1\r\nST: %s\r\n\r\n' \
    "${SSDP_HOST_HEADER}" "${st}")"

  response="$(printf '%s' "$request" | nc -u -w "${SSDP_TIMEOUT_SEC}" "${SSDP_TARGET_IP}" "${SSDP_PORT}" || true)"
  [[ -n "$response" ]] || fail "no SSDP response for ST=${st} (target=${SSDP_TARGET_IP}:${SSDP_PORT})"

  if [[ "$VERBOSE" == "1" ]]; then
    log "SSDP response for ST=${st}:"
    printf '%s\n' "$response"
  fi

  printf '%s' "$response" | grep -Eiq '^HTTP/1\.1 200 OK' || fail "SSDP response is not HTTP 200 for ST=${st}"
  printf '%s' "$response" | grep -Eiq '^LOCATION:[[:space:]]*http://[^[:space:]]+:'"${PORT}"'/device\.xml' \
    || fail "SSDP response missing valid LOCATION for ST=${st}"
  printf '%s' "$response" | grep -Eiq '^ST:[[:space:]]*'"${expected_st}"'$' \
    || fail "SSDP response ST mismatch for ST=${st}, expected ${expected_st}"
  printf '%s' "$response" | grep -Eiq '^USN:[[:space:]]*'"${expected_uuid}" \
    || fail "SSDP response USN does not include UUID for ST=${st}"
}

main() {
  require_cmd curl
  require_cmd grep
  require_cmd sed
  require_cmd tr
  require_cmd tail
  require_cmd nc

  log "HTTP target: ${BASE_URL}"
  log "SSDP target: ${SSDP_TARGET_IP}:${SSDP_PORT}"

  log "1/6 GET /device.xml"
  http_get "/device.xml"
  assert_http_200
  assert_body_contains "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
  assert_body_contains "<serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>"
  assert_body_contains "<serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType>"
  assert_body_contains "<serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType>"

  local device_xml
  local device_type
  local udn
  device_xml="$LAST_BODY"
  device_type="$(extract_tag_value "$device_xml" "deviceType")"
  udn="$(extract_tag_value "$device_xml" "UDN")"
  [[ -n "$device_type" ]] || fail "failed to parse deviceType from /device.xml"
  [[ -n "$udn" ]] || fail "failed to parse UDN from /device.xml"

  log "2/6 GET /scpd/AVTransport.xml"
  http_get "/scpd/AVTransport.xml"
  assert_http_200
  assert_body_contains "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">"
  assert_body_contains "<name>SetAVTransportURI</name>"

  log "3/6 GET /scpd/RenderingControl.xml"
  http_get "/scpd/RenderingControl.xml"
  assert_http_200
  assert_body_contains "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">"
  assert_body_contains "<name>GetVolume</name>"

  log "4/6 GET /scpd/ConnectionManager.xml"
  http_get "/scpd/ConnectionManager.xml"
  assert_http_200
  assert_body_contains "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">"
  assert_body_contains "<name>GetProtocolInfo</name>"

  log "5/6 SSDP probe ST=ssdp:all"
  ssdp_probe "ssdp:all" "$device_type" "$udn"

  log "6/6 SSDP probe ST=upnp:rootdevice"
  ssdp_probe "upnp:rootdevice" "upnp:rootdevice" "$udn"

  log "PASS"
}

main "$@"
