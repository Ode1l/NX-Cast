#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LOG_DIR="${PROJECT_ROOT}/logs"
SMOKE_SCRIPT="${SCRIPT_DIR}/dlna_soap_smoke.sh"

mkdir -p "$LOG_DIR"
LOG_FILE="${LOG_DIR}/$(basename "${BASH_SOURCE[0]}" .sh)-$(date +%Y%m%d-%H%M%S).log"
exec > >(tee -a "$LOG_FILE") 2>&1

HOST="${HOST:-192.168.1.7:49152}"
INSTANCE_ID="${INSTANCE_ID:-0}"
CHANNEL="${CHANNEL:-Master}"
SEEK_TARGET="${SEEK_TARGET:-00:00:01}"
STATE_WAIT_SECONDS="${STATE_WAIT_SECONDS:-15}"
HLS_STATE_WAIT_SECONDS="${HLS_STATE_WAIT_SECONDS:-60}"
STATE_POLL_INTERVAL="${STATE_POLL_INTERVAL:-1}"
HLS_READY_GRACE_RETRIES="${HLS_READY_GRACE_RETRIES:-5}"
HLS_READY_GRACE_INTERVAL="${HLS_READY_GRACE_INTERVAL:-0.2}"

declare -a STREAM_NAMES=()
declare -a STREAM_URLS=()

log() {
  printf '[hls-matrix] %s\n' "$*"
}

trap 'printf "[hls-matrix] log saved: %s\n" "$LOG_FILE"' EXIT

fail() {
  printf '[hls-matrix] ERROR: %s\n' "$*" >&2
  exit 1
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"
}

append_stream() {
  local name="$1"
  local url="$2"

  STREAM_NAMES+=("$name")
  STREAM_URLS+=("$url")
}

default_streams() {
  # Public test streams verified on 2026-04-02.
  append_stream "CCTV6" "http://107.150.60.122/live/cctv6hd.m3u8"
}

sanitize_name() {
  local value="$1"
  printf '%s' "$value" | tr '/: ?&=' '-' | tr -cd '[:alnum:]_.-\n'
}

parse_custom_streams() {
  local entry=""
  local name=""
  local url=""

  if [ "$#" -eq 0 ]; then
    default_streams
    return
  fi

  for entry in "$@"; do
    if [[ "$entry" == *=* ]]; then
      name="${entry%%=*}"
      url="${entry#*=}"
    else
      url="$entry"
      name="$(sanitize_name "${url##*/}")"
      if [ -z "$name" ]; then
        name="custom-$((${#STREAM_NAMES[@]} + 1))"
      fi
    fi

    if [ -z "$url" ]; then
      fail "empty stream url in argument: $entry"
    fi
    append_stream "$name" "$url"
  done
}

run_one() {
  local name="$1"
  local url="$2"
  local status=0

  log "start name=${name} url=${url}"
  set +e
  HOST="$HOST" \
  INSTANCE_ID="$INSTANCE_ID" \
  CHANNEL="$CHANNEL" \
  MEDIA_URI="$url" \
  SEEK_TARGET="$SEEK_TARGET" \
  STATE_WAIT_SECONDS="$STATE_WAIT_SECONDS" \
  HLS_STATE_WAIT_SECONDS="$HLS_STATE_WAIT_SECONDS" \
  STATE_POLL_INTERVAL="$STATE_POLL_INTERVAL" \
  HLS_READY_GRACE_RETRIES="$HLS_READY_GRACE_RETRIES" \
  HLS_READY_GRACE_INTERVAL="$HLS_READY_GRACE_INTERVAL" \
  "$SMOKE_SCRIPT"
  status=$?
  set -e

  if [ "$status" -eq 0 ]; then
    log "result name=${name} status=PASS"
  else
    log "result name=${name} status=FAIL exit=${status}"
  fi

  return "$status"
}

main() {
  local i=0
  local total=0
  local passed=0
  local failed=0

  require_cmd bash
  require_cmd curl
  [ -x "$SMOKE_SCRIPT" ] || fail "smoke script not executable: $SMOKE_SCRIPT"

  parse_custom_streams "$@"
  total="${#STREAM_NAMES[@]}"

  log "log file: ${LOG_FILE}"
  log "target: http://${HOST}"
  log "streams: ${total}"

  for ((i = 0; i < total; i++)); do
    if run_one "${STREAM_NAMES[$i]}" "${STREAM_URLS[$i]}"; then
      passed=$((passed + 1))
    else
      failed=$((failed + 1))
    fi
  done

  log "summary passed=${passed} failed=${failed} total=${total}"
  [ "$failed" -eq 0 ] || return 1
}

main "$@"
