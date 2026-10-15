#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/scripts/dev_environment.sh"
nxcast_load_sdk
exec make -C "$ROOT/tools/miracast_probe"
