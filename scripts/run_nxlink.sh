#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
LOG_DIR="${PROJECT_ROOT}/logs"
mkdir -p "$LOG_DIR"
LOG_FILE="${LOG_DIR}/$(basename "${BASH_SOURCE[0]}" .sh)-$(date +%Y%m%d-%H%M%S).log"
exec > >(tee -a "$LOG_FILE") 2>&1
trap 'printf "[run-nxlink] log saved: %s\n" "$LOG_FILE"' EXIT

NXLINK_BIN=${NXLINK_BIN:-nxlink}
NRO_PATH=${1:-${PROJECT_ROOT}/NX-Cast.nro}
SWITCH_IP=${SWITCH_IP:-}

printf '[run-nxlink] log file: %s\n' "$LOG_FILE"
if [ ! -f "$NRO_PATH" ]; then
  echo "NRO not found: $NRO_PATH" >&2
  exit 1
fi
if [ -n "$SWITCH_IP" ]; then
  echo "[run-nxlink] target=${SWITCH_IP} server=on"
  "$NXLINK_BIN" -a "$SWITCH_IP" -s "$NRO_PATH"
else
  echo "[run-nxlink] target=auto server=on"
  "$NXLINK_BIN" -s "$NRO_PATH"
fi
