#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$ROOT"
ACTION=${1:-help}
case "$ACTION" in
    publish) exec bash "$ROOT/scripts/publish_release.sh" ;;
    package) NXCAST_MIN_NRO_SIZE=5000000 exec sh "$ROOT/scripts/package_release.sh" ;;
    clean|build|trace|release-build|upload|upload-log) ;;
    *) echo 'Usage: dev.sh {clean|build|trace|release-build|upload|upload-log|package|publish}'
       [[ "$ACTION" == help ]] && exit 0
       exit 2 ;;
esac
source "$ROOT/scripts/dev_environment.sh"
nxcast_load_sdk
case "$ACTION" in
    clean) exec make clean ;;
    build) exec make dev-build BUILD_JOBS="${BUILD_JOBS:-4}" TRACE_MEDIA=0 TRACE_INPUT=0 TRACE_AIRPLAY=0 ;;
    trace) exec make full-trace-build BUILD_JOBS="${BUILD_JOBS:-4}" TRACE_MEDIA=1 TRACE_INPUT=1 TRACE_AIRPLAY=1 ;;
    release-build) exec make release-build RELEASE_JOBS="${BUILD_JOBS:-4}" ;;
    upload) NO_BUILD=1 NXLINK_SERVER=0 exec bash "$ROOT/scripts/run_nxlink.sh" ;;
    upload-log) NO_BUILD=1 NXLINK_SERVER=1 exec bash "$ROOT/scripts/run_nxlink.sh" ;;
esac
