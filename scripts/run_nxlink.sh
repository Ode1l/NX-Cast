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
BUILD_MAKE_ARGS=${BUILD_MAKE_ARGS:-"NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1"}
NXLINK_SERVER=${NXLINK_SERVER:-1}
NXLINK_RETRIES=${NXLINK_RETRIES:-10}
LOG_STAMP="$(date +%Y%m%d-%H%M%S)"
SESSION_LOG="${LOG_DIR}/run_nxlink-${LOG_STAMP}.log"
NXLINK_ARGS=()
NXLINK_OPTS=(-r "$NXLINK_RETRIES")

if command -v stdbuf >/dev/null 2>&1; then
  NXLINK_ARGS+=(stdbuf -oL -eL)
fi

if [ "$NO_BUILD" != "1" ]; then
  echo "[run-nxlink] build=make clean && make ${BUILD_MAKE_ARGS} -j${BUILD_JOBS}"
  cd "$PROJECT_ROOT"
  source /opt/devkitpro/switchvars.sh
  make clean
  # shellcheck disable=SC2086
  make ${BUILD_MAKE_ARGS} -j"${BUILD_JOBS}"
fi

if [ ! -f "$NRO_PATH" ]; then
  echo "[run-nxlink] NRO not found: $NRO_PATH" >&2
  exit 1
fi

cd "$PROJECT_ROOT"

echo "[run-nxlink] session_log=${SESSION_LOG}"

set +e
if [ "$NXLINK_SERVER" = "1" ] && [ -n "$SWITCH_IP" ]; then
  echo "[run-nxlink] target=${SWITCH_IP} mode=server"
  "${NXLINK_ARGS[@]}" "$NXLINK_BIN" -a "$SWITCH_IP" "${NXLINK_OPTS[@]}" -s "$NRO_PATH" 2>&1 | tee "$SESSION_LOG"
elif [ "$NXLINK_SERVER" = "1" ]; then
  echo "[run-nxlink] mode=server"
  "${NXLINK_ARGS[@]}" "$NXLINK_BIN" "${NXLINK_OPTS[@]}" -s "$NRO_PATH" 2>&1 | tee "$SESSION_LOG"
elif [ -n "$SWITCH_IP" ]; then
  echo "[run-nxlink] target=${SWITCH_IP} mode=upload"
  "${NXLINK_ARGS[@]}" "$NXLINK_BIN" -a "$SWITCH_IP" "${NXLINK_OPTS[@]}" "$NRO_PATH" 2>&1 | tee "$SESSION_LOG"
else
  echo "[run-nxlink] mode=upload"
  "${NXLINK_ARGS[@]}" "$NXLINK_BIN" "${NXLINK_OPTS[@]}" "$NRO_PATH" 2>&1 | tee "$SESSION_LOG"
fi
nxlink_status=${PIPESTATUS[0]}
set -e

if [ "$nxlink_status" -ne 0 ]; then
  echo "[run-nxlink] nxlink exited with status ${nxlink_status}"
fi

exit "$nxlink_status"
