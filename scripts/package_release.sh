#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIST_DIR="${ROOT_DIR}/dist"
SDMC_DIR="${DIST_DIR}/sdmc"
PACKAGE="${DIST_DIR}/NX-Cast-sdmc.zip"
IPTV_SOURCES="${ROOT_DIR}/assets/iptv/sources.txt"
PACKAGED_IPTV_SOURCES="${SDMC_DIR}/switch/NX-Cast/iptv/sources.txt"

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

if [ ! -s "${IPTV_SOURCES}" ]; then
    echo "assets/iptv/sources.txt is missing or empty. Refusing to publish an SD package without the IPTV presets." >&2
    exit 1
fi

rm -rf "${SDMC_DIR}" "${PACKAGE}"
mkdir -p "${SDMC_DIR}/switch/NX-Cast/dlna" "${SDMC_DIR}/switch/NX-Cast/fonts" "${SDMC_DIR}/switch/NX-Cast/iptv"

cp "${ROOT_DIR}/NX-Cast.nro" "${SDMC_DIR}/switch/NX-Cast/NX-Cast.nro"
cp "${ROOT_DIR}/assets/dlna/"* "${SDMC_DIR}/switch/NX-Cast/dlna/"
if [ -d "${ROOT_DIR}/assets/fonts" ]; then
    cp "${ROOT_DIR}/assets/fonts/"* "${SDMC_DIR}/switch/NX-Cast/fonts/"
fi
if [ -d "${ROOT_DIR}/assets/iptv" ]; then
    cp "${ROOT_DIR}/assets/iptv/"* "${SDMC_DIR}/switch/NX-Cast/iptv/"
fi

if [ ! -s "${PACKAGED_IPTV_SOURCES}" ] || ! cmp -s "${IPTV_SOURCES}" "${PACKAGED_IPTV_SOURCES}"; then
    echo "IPTV sources.txt was not copied intact into the staged SD package." >&2
    exit 1
fi

cat > "${SDMC_DIR}/README-NX-Cast.txt" <<'EOF'
NX-Cast install
===============

Recommended install:
1. Extract this zip to the root of your Switch SD card.
2. Confirm the final path is:
   sdmc:/switch/NX-Cast/NX-Cast.nro
3. Launch NX-Cast from hbmenu.

Do not extract this zip into an extra nested folder such as:
sdmc:/NX-Cast-sdmc/switch/NX-Cast/

Included files:
- switch/NX-Cast/NX-Cast.nro
- switch/NX-Cast/dlna/ runtime DLNA description assets
- switch/NX-Cast/fonts/ packaged UI font and license notices
- switch/NX-Cast/iptv/sources.txt preinstalled IPTV sources
- switch/NX-Cast/iptv/ local playlists, remote source/cache location, and usage notes

If NX-Cast does not appear in hbmenu, check the SD card path above.
If the UI font looks wrong, reinstall this full SD package instead of copying
only the NRO.
EOF

mkdir -p "${DIST_DIR}"
cp "${ROOT_DIR}/NX-Cast.nro" "${DIST_DIR}/NX-Cast.nro"

(
    cd "${SDMC_DIR}"
    zip -qr "${PACKAGE}" README-NX-Cast.txt switch
)

if ! zip -sf "${PACKAGE}" | grep -Fq "  switch/NX-Cast/iptv/sources.txt"; then
    echo "IPTV sources.txt is missing from the release ZIP." >&2
    exit 1
fi

echo "Verified IPTV presets in ${PACKAGE}"
echo "Packaged ${PACKAGE}"
