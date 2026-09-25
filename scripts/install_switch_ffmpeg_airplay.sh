#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUTPUT_DIR=${NXCAST_FFMPEG_OUTPUT_DIR:-"${ROOT_DIR}/build/toolchain/ffmpeg"}
PACKAGE_NAME=switch-ffmpeg-7.1-3-any.pkg.tar.zst
PACKAGE_PATH="${OUTPUT_DIR}/${PACKAGE_NAME}"
PREFIX=${PORTLIBS_PREFIX:-"${DEVKITPRO:-/opt/devkitpro}/portlibs/switch"}

if [[ ${EUID} -eq 0 ]]; then
    echo "Run this script as a regular user. It invokes sudo only for package installation." >&2
    exit 1
fi

PACMAN=$(command -v dkp-pacman || true)
if [[ -z ${PACMAN} ]]; then
    echo "dkp-pacman not found. Source /opt/devkitpro/switchvars.sh first." >&2
    exit 1
fi

"${ROOT_DIR}/scripts/fetch_switch_ffmpeg_airplay.sh" "${OUTPUT_DIR}"

echo "Installing ${PACKAGE_NAME} into the global devkitPro Switch prefix"
sudo "${PACMAN}" -U --noconfirm "${PACKAGE_PATH}"

"${ROOT_DIR}/scripts/verify_switch_ffmpeg_airplay.sh" "${PREFIX}"
echo "Global Switch FFmpeg installation is ready for make release-build."
