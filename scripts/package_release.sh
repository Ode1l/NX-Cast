#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIST_DIR="${ROOT_DIR}/dist"
SDMC_DIR="${DIST_DIR}/sdmc"
PACKAGE="${DIST_DIR}/NX-Cast-sdmc.zip"

if [ ! -f "${ROOT_DIR}/NX-Cast.nro" ]; then
    echo "NX-Cast.nro not found. Run make before packaging." >&2
    exit 1
fi

if [ "${NXCAST_MIN_NRO_SIZE:-0}" -gt 0 ]; then
    actual_size=$(wc -c < "${ROOT_DIR}/NX-Cast.nro" | tr -d ' ')
    if [ "${actual_size}" -lt "${NXCAST_MIN_NRO_SIZE}" ]; then
        echo "NX-Cast.nro is too small (${actual_size} bytes). Expected at least ${NXCAST_MIN_NRO_SIZE} bytes." >&2
        exit 1
    fi
fi

if [ ! -d "${ROOT_DIR}/assets/dlna" ]; then
    echo "assets/dlna not found. Cannot package runtime DLNA assets." >&2
    exit 1
fi

rm -rf "${SDMC_DIR}" "${PACKAGE}"
mkdir -p "${SDMC_DIR}/switch/NX-Cast/dlna" "${SDMC_DIR}/switch/NX-Cast/fonts"

cp "${ROOT_DIR}/NX-Cast.nro" "${SDMC_DIR}/switch/NX-Cast/NX-Cast.nro"
cp "${ROOT_DIR}/assets/dlna/"* "${SDMC_DIR}/switch/NX-Cast/dlna/"
if [ -d "${ROOT_DIR}/assets/fonts" ]; then
    cp "${ROOT_DIR}/assets/fonts/"* "${SDMC_DIR}/switch/NX-Cast/fonts/"
fi

mkdir -p "${DIST_DIR}"
cp "${ROOT_DIR}/NX-Cast.nro" "${DIST_DIR}/NX-Cast.nro"

(
    cd "${SDMC_DIR}"
    zip -qr "${PACKAGE}" switch
)

echo "Packaged ${PACKAGE}"
