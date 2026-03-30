#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LOG_DIR="${PROJECT_ROOT}/logs"
mkdir -p "$LOG_DIR"

NXLINK_BIN=${NXLINK_BIN:-nxlink}
NRO_PATH=${1:-${PROJECT_ROOT}/NX-Cast.nro}
SWITCH_IP=${SWITCH_IP:-}
NO_BUILD=${NO_BUILD:-0}
BUILD_JOBS=${BUILD_JOBS:-4}
LOG_TCP_PORT=${LOG_TCP_PORT:-28772}
START_LOG_RECEIVER=${START_LOG_RECEIVER:-1}
SWITCH_IP_SOURCE="env"
LOG_RECEIVER_PID=""

stop_log_receiver() {
  if [ -n "$LOG_RECEIVER_PID" ] && kill -0 "$LOG_RECEIVER_PID" >/dev/null 2>&1; then
    kill "$LOG_RECEIVER_PID" >/dev/null 2>&1 || true
    wait "$LOG_RECEIVER_PID" >/dev/null 2>&1 || true
  fi
}

trap 'stop_log_receiver' EXIT

detect_switch_ip_from_logs() {
  local latest_log
  latest_log="$(find "$LOG_DIR" -maxdepth 1 -type f -name 'remote-log-*.log' | sort | tail -n 1 || true)"
  if [ -z "$latest_log" ] || [ ! -f "$latest_log" ]; then
    return 1
  fi

  local detected_ip
  detected_ip="$(grep -Eo 'http://([0-9]{1,3}\.){3}[0-9]{1,3}(:[0-9]+)?' "$latest_log" | tail -n 1 | sed -E 's#http://(([0-9]{1,3}\.){3}[0-9]{1,3})(:[0-9]+)?#\1#' || true)"
  if [ -z "$detected_ip" ]; then
    return 1
  fi

  printf '%s\n' "$detected_ip"
}

start_log_receiver() {
  if ! command -v python3 >/dev/null 2>&1; then
    return 1
  fi

  python3 -u "${SCRIPT_DIR}/log_receiver.py" --port "$LOG_TCP_PORT" --stdout --once &
  LOG_RECEIVER_PID=$!
}

if [ "$NO_BUILD" != "1" ]; then
  echo "[run-nxlink] build=make clean && make -j${BUILD_JOBS}"
  (
    cd "$PROJECT_ROOT"
    source /opt/devkitpro/switchvars.sh
    make clean
    make -j"${BUILD_JOBS}"
  )
fi

if [ ! -f "$NRO_PATH" ]; then
  echo "[run-nxlink] NRO not found: $NRO_PATH" >&2
  exit 1
fi

if [ "$START_LOG_RECEIVER" = "1" ]; then
  start_log_receiver || echo "[run-nxlink] tcp-log receiver unavailable"
else
  echo "[run-nxlink] remote log receiver not started by script; run scripts/log_receiver.py --port ${LOG_TCP_PORT}"
fi

if [ -z "$SWITCH_IP" ]; then
  SWITCH_IP="$(detect_switch_ip_from_logs || true)"
  SWITCH_IP_SOURCE="log-cache"
fi

if [ -n "$SWITCH_IP" ]; then
  echo "[run-nxlink] target=${SWITCH_IP} source=${SWITCH_IP_SOURCE} tcp-log=${LOG_TCP_PORT}"
  "$NXLINK_BIN" -a "$SWITCH_IP" "$NRO_PATH"
else
  echo "[run-nxlink] target=auto tcp-log=${LOG_TCP_PORT}"
  "$NXLINK_BIN" "$NRO_PATH"
fi

if [ -n "$LOG_RECEIVER_PID" ]; then
  echo "[run-nxlink] upload complete; waiting for remote log upload on exit"
  wait "$LOG_RECEIVER_PID" || true
fi
