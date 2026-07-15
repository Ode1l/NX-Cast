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

if [ ! -d "${ROOT_DIR}/assets/dlna" ]; then
    echo "assets/dlna not found. Cannot package runtime DLNA assets." >&2
    exit 1
fi

rm -rf "${SDMC_DIR}" "${PACKAGE}"
mkdir -p "${SDMC_DIR}/switch/NX-Cast/dlna"

cp "${ROOT_DIR}/NX-Cast.nro" "${SDMC_DIR}/switch/NX-Cast/NX-Cast.nro"
cp "${ROOT_DIR}/assets/dlna/"* "${SDMC_DIR}/switch/NX-Cast/dlna/"

mkdir -p "${DIST_DIR}"
cp "${ROOT_DIR}/NX-Cast.nro" "${DIST_DIR}/NX-Cast.nro"

(
    cd "${SDMC_DIR}"
    zip -qr "${PACKAGE}" switch
)

echo "Packaged ${PACKAGE}"
