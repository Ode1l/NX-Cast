#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LOG_DIR="${PROJECT_ROOT}/logs"
mkdir -p "$LOG_DIR"
LOG_FILE="${LOG_DIR}/$(basename "${BASH_SOURCE[0]}" .sh)-$(date +%Y%m%d-%H%M%S).log"
exec > >(tee -a "$LOG_FILE") 2>&1

NXLINK_BIN=${NXLINK_BIN:-nxlink}
NRO_PATH=${1:-${PROJECT_ROOT}/NX-Cast.nro}
SWITCH_IP=${SWITCH_IP:-}
NO_BUILD=${NO_BUILD:-0}
BUILD_JOBS=${BUILD_JOBS:-4}
LOG_TCP_PORT=${LOG_TCP_PORT:-28772}
SWITCH_IP_SOURCE="env"
LOG_RECEIVER_PID=""

stop_log_receiver() {
  if [ -n "$LOG_RECEIVER_PID" ] && kill -0 "$LOG_RECEIVER_PID" >/dev/null 2>&1; then
    kill "$LOG_RECEIVER_PID" >/dev/null 2>&1 || true
    wait "$LOG_RECEIVER_PID" >/dev/null 2>&1 || true
  fi
}

trap 'stop_log_receiver; printf "[run-nxlink] log saved: %s\n" "$LOG_FILE"' EXIT

detect_switch_ip_from_logs() {
  local latest_log
  latest_log="$(find "$LOG_DIR" -maxdepth 1 -type f -name 'run_nxlink-*.log' ! -name "$(basename "$LOG_FILE")" | sort | tail -n 1 || true)"
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

  python3 -u - "$LOG_TCP_PORT" <<'PY' &
import socket
import sys

port = int(sys.argv[1])
server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(("0.0.0.0", port))
server.listen(1)
print(f"[run-nxlink] tcp-log listening on :{port}", flush=True)
try:
    while True:
        conn, addr = server.accept()
        print(f"[run-nxlink] tcp-log client {addr[0]}:{addr[1]}", flush=True)
        with conn:
            buffer = b""
            while True:
                data = conn.recv(4096)
                if not data:
                    break
                buffer += data
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    text = line.decode("utf-8", errors="replace").rstrip()
                    if text:
                        print(text, flush=True)
except KeyboardInterrupt:
    pass
finally:
    server.close()
PY
  LOG_RECEIVER_PID=$!
}

printf '[run-nxlink] log file: %s\n' "$LOG_FILE"

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

start_log_receiver || echo "[run-nxlink] tcp-log receiver unavailable"

if [ -z "$SWITCH_IP" ]; then
  SWITCH_IP="$(detect_switch_ip_from_logs || true)"
  SWITCH_IP_SOURCE="log-cache"
fi

if [ -n "$SWITCH_IP" ]; then
  echo "[run-nxlink] target=${SWITCH_IP} source=${SWITCH_IP_SOURCE} server=on tcp-log=${LOG_TCP_PORT}"
  "$NXLINK_BIN" -a "$SWITCH_IP" -s "$NRO_PATH"
else
  echo "[run-nxlink] target=auto server=on tcp-log=${LOG_TCP_PORT}"
  "$NXLINK_BIN" -s "$NRO_PATH"
fi
