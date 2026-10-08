#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUTPUT_DIR=${NXCAST_FFMPEG_OUTPUT_DIR:-"${ROOT_DIR}/artifacts/toolchain/ffmpeg"}
PACKAGE_NAME=$("${ROOT_DIR}/scripts/fetch_switch_ffmpeg_airplay.sh" --package-name)
PACKAGE_VERSION=${PACKAGE_NAME#switch-ffmpeg-}
PACKAGE_VERSION=${PACKAGE_VERSION%-any.pkg.tar.zst}
PACKAGE_PATH="${OUTPUT_DIR}/${PACKAGE_NAME}"
DEFAULT_PREFIX="${DEVKITPRO:-/opt/devkitpro}/portlibs/switch"
PREFIX=${PORTLIBS_PREFIX:-"${DEFAULT_PREFIX}"}

if [[ ${PREFIX%/} != "${DEFAULT_PREFIX%/}" ]]; then
    echo "Automatic FFmpeg installation targets ${DEFAULT_PREFIX}, not ${PREFIX}." >&2
    echo "For a separately managed toolchain, build with NXCAST_AUTO_INSTALL_FFMPEG=0." >&2
    exit 1
fi

PACMAN=$(command -v dkp-pacman || true)
WINDOWS_MSYS=0
case "$(uname -s)" in
    MSYS*|MINGW*)
        WINDOWS_MSYS=1
        PACMAN=$(command -v pacman || true)
        ;;
esac
if [[ -z ${PACMAN} ]]; then
    echo "devkitPro package manager not found. Initialize the SDK or use devkitPro MSYS2 on Windows." >&2
    exit 1
fi

package_is_ready() {
    local installed archive
    installed=$("${PACMAN}" -Q switch-ffmpeg 2>/dev/null) || return 1
    [[ ${installed} == "switch-ffmpeg ${PACKAGE_VERSION}" ]] || return 1
    for archive in libavcodec.a libavformat.a libavutil.a; do
        [[ -s "${PREFIX}/lib/${archive}" ]] || return 1
    done
    [[ -s "${PREFIX}/include/libavutil/hwcontext_nvtegra.h" ]]
}

if package_is_ready; then
    echo "[ffmpeg] ${PACKAGE_VERSION} already installed; continuing without download or installation."
    exit 0
fi

"${ROOT_DIR}/scripts/fetch_switch_ffmpeg_airplay.sh" "${OUTPUT_DIR}"

echo "Installing ${PACKAGE_NAME} into the global devkitPro Switch prefix"
if [[ ${EUID} -eq 0 || ${WINDOWS_MSYS} -eq 1 ]]; then
    "${PACMAN}" -U --noconfirm "${PACKAGE_PATH}"
else
    sudo "${PACMAN}" -U --noconfirm "${PACKAGE_PATH}"
fi

if ! package_is_ready; then
    echo "FFmpeg installation did not provide switch-ffmpeg ${PACKAGE_VERSION} at ${PREFIX}." >&2
    exit 1
fi

"${ROOT_DIR}/scripts/verify_switch_ffmpeg_airplay.sh" "${PREFIX}"
echo "Global Switch FFmpeg installation is ready for make release-build."
