#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIST_DIR="${ROOT_DIR}/dist"
SDMC_DIR="${DIST_DIR}/sdmc"
PACKAGE="${DIST_DIR}/NX-Cast-sdmc.zip"
IPTV_SOURCES="${ROOT_DIR}/assets/iptv/sources.txt"
PACKAGED_IPTV_SOURCES="${SDMC_DIR}/switch/NX-Cast/iptv/sources.txt"
AIRPLAY_README="${ROOT_DIR}/assets/airplay/README.txt"
PACKAGED_AIRPLAY_README="${SDMC_DIR}/switch/NX-Cast/airplay/README.txt"
RELEASE_ATTESTATION="${ROOT_DIR}/build/release-features.txt"

if [ "${NXCAST_ALLOW_UNVERIFIED_PACKAGE:-0}" != "1" ]; then
    if [ ! -f "${RELEASE_ATTESTATION}" ] || \
       ! grep -Fxq 'nxcast-release-v1' "${RELEASE_ATTESTATION}" || \
       ! grep -Fxq 'libmpv=1' "${RELEASE_ATTESTATION}" || \
       ! grep -Fxq 'deko3d=1' "${RELEASE_ATTESTATION}" || \
       ! grep -Fxq 'airplay-ed25519=1' "${RELEASE_ATTESTATION}"; then
        echo "Release build attestation is missing or incomplete. Run make release-build first." >&2
        exit 1
    fi
fi

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

if [ ! -s "${AIRPLAY_README}" ]; then
    echo "assets/airplay/README.txt is missing or empty." >&2
    exit 1
fi

rm -rf "${SDMC_DIR}" "${PACKAGE}"
mkdir -p "${SDMC_DIR}/switch/NX-Cast/dlna" \
    "${SDMC_DIR}/switch/NX-Cast/fonts" \
    "${SDMC_DIR}/switch/NX-Cast/iptv" \
    "${SDMC_DIR}/switch/NX-Cast/airplay" \
    "${SDMC_DIR}/switch/NX-Cast/licenses"

cp "${ROOT_DIR}/NX-Cast.nro" "${SDMC_DIR}/switch/NX-Cast/NX-Cast.nro"
cp "${ROOT_DIR}/assets/dlna/"* "${SDMC_DIR}/switch/NX-Cast/dlna/"
if [ -d "${ROOT_DIR}/assets/fonts" ]; then
    cp "${ROOT_DIR}/assets/fonts/"* "${SDMC_DIR}/switch/NX-Cast/fonts/"
fi
if [ -d "${ROOT_DIR}/assets/iptv" ]; then
    cp "${ROOT_DIR}/assets/iptv/"* "${SDMC_DIR}/switch/NX-Cast/iptv/"
fi
cp "${ROOT_DIR}/assets/airplay/"* "${SDMC_DIR}/switch/NX-Cast/airplay/"
if [ -d "${ROOT_DIR}/assets/licenses" ]; then
    cp "${ROOT_DIR}/assets/licenses/"* "${SDMC_DIR}/switch/NX-Cast/licenses/"
fi
cp "${ROOT_DIR}/LICENSE" "${SDMC_DIR}/switch/NX-Cast/licenses/LICENSE.NX-Cast.txt"
cp "${ROOT_DIR}/third_party/NOTICE.md" "${SDMC_DIR}/switch/NX-Cast/licenses/THIRD-PARTY-NOTICE.txt"
cp "${ROOT_DIR}/third_party/imgui/LICENSE.txt" "${SDMC_DIR}/switch/NX-Cast/licenses/LICENSE.Dear-ImGui.txt"

if [ ! -s "${PACKAGED_IPTV_SOURCES}" ] || ! cmp -s "${IPTV_SOURCES}" "${PACKAGED_IPTV_SOURCES}"; then
    echo "IPTV sources.txt was not copied intact into the staged SD package." >&2
    exit 1
fi

if [ ! -s "${PACKAGED_AIRPLAY_README}" ] || ! cmp -s "${AIRPLAY_README}" "${PACKAGED_AIRPLAY_README}"; then
    echo "AirPlay README was not copied intact into the staged SD package." >&2
    exit 1
fi

sensitive_files=$(find "${SDMC_DIR}" -type f \( \
    -iname 'identity.bin' -o -iname 'pairings.bin' -o \
    -iname '*.key' -o -iname '*.pem' -o -iname '*.p12' -o \
    -iname '*.log' -o -iname '*.trace' -o -iname '*.dmp' -o \
    -iname '*.dump' -o -iname '*.pcap' -o -iname '*.pcapng' \
    \) -print)
if [ -n "${sensitive_files}" ]; then
    echo "Refusing to package runtime secrets or diagnostic captures:" >&2
    printf '%s\n' "${sensitive_files}" >&2
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
- switch/NX-Cast/airplay/ AirPlay storage notice; identity and pairings are generated on first launch
- switch/NX-Cast/licenses/ third-party license notices

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

if ! zip -sf "${PACKAGE}" | grep -Fq "  switch/NX-Cast/airplay/README.txt"; then
    echo "AirPlay README is missing from the release ZIP." >&2
    exit 1
fi

if ! zip -sf "${PACKAGE}" | grep -Fq "  switch/NX-Cast/licenses/LICENSE.NX-Cast.txt"; then
    echo "NX-Cast license is missing from the release ZIP." >&2
    exit 1
fi

echo "Verified IPTV presets in ${PACKAGE}"
echo "Verified AirPlay storage skeleton and sensitive-file exclusions in ${PACKAGE}"
echo "Packaged ${PACKAGE}"
