#!/bin/sh
set -e
NXLINK_BIN=${NXLINK_BIN:-nxlink}
NRO_PATH=${1:-$(dirname "$0")/../NX-Cast.nro}
if [ ! -f "$NRO_PATH" ]; then
  echo "NRO not found: $NRO_PATH" >&2
  exit 1
fi
"$NXLINK_BIN" "$NRO_PATH"
