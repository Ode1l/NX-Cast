#!/usr/bin/env bash
set -euo pipefail

DEFAULT_PREFIX="${DEVKITPRO:-/opt/devkitpro}/portlibs/switch"
PREFIX=${1:-${PORTLIBS_PREFIX:-${DEFAULT_PREFIX}}}
NM=${SWITCH_NM:-aarch64-none-elf-nm}
LIBAVCODEC="${PREFIX}/lib/libavcodec.a"
LIBAVFORMAT="${PREFIX}/lib/libavformat.a"
LIBAVUTIL="${PREFIX}/lib/libavutil.a"

if ! command -v "${NM}" >/dev/null 2>&1; then
    DEVKIT_NM="${DEVKITPRO:-/opt/devkitpro}/devkitA64/bin/aarch64-none-elf-nm"
    if [[ ${NM} == aarch64-none-elf-nm && -x ${DEVKIT_NM} ]]; then
        NM=${DEVKIT_NM}
    else
        echo "Switch nm tool not found: ${NM}" >&2
        exit 1
    fi
fi

for archive in "${LIBAVCODEC}" "${LIBAVFORMAT}" "${LIBAVUTIL}"; do
    if [[ ! -f ${archive} ]]; then
        echo "Required Switch FFmpeg archive not found: ${archive}" >&2
        exit 1
    fi
done

WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/nxcast-ffmpeg-verify.XXXXXX")
cleanup() {
    rm -rf -- "${WORK_DIR}"
}
trap cleanup EXIT

"${NM}" -g --defined-only "${LIBAVCODEC}" > "${WORK_DIR}/libavcodec.symbols"
"${NM}" -g --defined-only "${LIBAVFORMAT}" > "${WORK_DIR}/libavformat.symbols"
"${NM}" -A -g --undefined-only "${LIBAVUTIL}" > "${WORK_DIR}/libavutil.undefined"

for symbol in ff_alac_decoder ff_h264_parser; do
    if ! grep -Eq "[[:space:]]${symbol}$" "${WORK_DIR}/libavcodec.symbols"; then
        echo "Switch FFmpeg is missing required libavcodec symbol: ${symbol}" >&2
        exit 1
    fi
done

if ! grep -Eq '[[:space:]]ff_matroska_muxer$' "${WORK_DIR}/libavformat.symbols"; then
    echo "Switch FFmpeg is missing required libavformat symbol: ff_matroska_muxer" >&2
    exit 1
fi

if ! grep -Eq 'random_seed\.o:.*[[:space:]]U[[:space:]]randomGet$' "${WORK_DIR}/libavutil.undefined"; then
    echo "Switch FFmpeg random_seed.o does not use libnx randomGet" >&2
    exit 1
fi

echo "Switch FFmpeg verified at ${PREFIX}: ALAC decoder, H.264 parser, Matroska muxer, libnx random"
