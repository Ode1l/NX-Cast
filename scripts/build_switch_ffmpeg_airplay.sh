#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUTPUT_DIR=${1:-"${ROOT_DIR}/build/toolchain/ffmpeg"}
VERIFY_SCRIPT=${NXCAST_FFMPEG_VERIFY_SCRIPT:-"${SCRIPT_DIR}/verify_switch_ffmpeg_airplay.sh"}

WILIWILI_COMMIT=88e5876bea9502d06f46a8656e3530684d3aaf7d
WILIWILI_ARCHIVE_SHA256=d141ba1a8e36ace4bd5ebdc1bd716549625af6e4baae4013d5b180786f31b921
FFMPEG_ARCHIVE_SHA256=6d62556767127bbf49c3b7d7c6fa55f1223be6139c0de66707305368eaad05db
FFMPEG_PATCH_SHA256=1792380b992e3554a4abcddf0d7b395bfd8c118ac7c6e38c8f2fb0d39753a390
NETWORK_PATCH_SHA256=150c56eff36b1179f5409bf2e30fbf6996ce603ba6c365b6bf94928f116c97fa
RANDOM_PATCH_SHA256=14a3d4107827cb42ee8ae91afb873afb11c3877f2b1dac5cf946030c8d446280
PACKAGE_NAME=switch-ffmpeg-7.1-3-any.pkg.tar.zst

if [[ ${EUID} -eq 0 ]]; then
    echo "Do not run this script as root; dkp-makepkg refuses root builds." >&2
    exit 1
fi

for command in curl tar awk grep find cp mkdir mktemp; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "Required command not found: ${command}" >&2
        exit 1
    fi
done

if ! command -v dkp-makepkg >/dev/null 2>&1; then
    echo "dkp-makepkg not found. Install devkitPro/devkitA64 first." >&2
    exit 1
fi

if [[ ! -x ${VERIFY_SCRIPT} ]]; then
    echo "FFmpeg verifier not found or not executable: ${VERIFY_SCRIPT}" >&2
    exit 1
fi

if [[ -z ${NXCAST_FFMPEG_JOBS:-} ]]; then
    if command -v getconf >/dev/null 2>&1; then
        NXCAST_FFMPEG_JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)
    fi
    if [[ -z ${NXCAST_FFMPEG_JOBS:-} ]] && command -v sysctl >/dev/null 2>&1; then
        NXCAST_FFMPEG_JOBS=$(sysctl -n hw.logicalcpu 2>/dev/null || true)
    fi
    NXCAST_FFMPEG_JOBS=${NXCAST_FFMPEG_JOBS:-4}
fi
export NXCAST_FFMPEG_JOBS

verify_sha256() {
    local expected=$1
    local file=$2
    local actual

    if command -v sha256sum >/dev/null 2>&1; then
        actual=$(sha256sum "${file}" | awk '{print $1}')
    elif command -v shasum >/dev/null 2>&1; then
        actual=$(shasum -a 256 "${file}" | awk '{print $1}')
    else
        echo "sha256sum or shasum is required." >&2
        return 1
    fi

    if [[ ${actual} != "${expected}" ]]; then
        echo "SHA-256 mismatch for ${file}" >&2
        echo "expected: ${expected}" >&2
        echo "actual:   ${actual}" >&2
        return 1
    fi
}

WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/nxcast-ffmpeg.XXXXXX")
cleanup() {
    if [[ -n ${WORK_DIR:-} && -d ${WORK_DIR} ]]; then
        rm -rf -- "${WORK_DIR}"
    fi
}
trap cleanup EXIT

ARCHIVE=${WORK_DIR}/wiliwili.tar.gz
SOURCE_DIR=${WORK_DIR}/source
RECIPE_DIR=${WORK_DIR}/ffmpeg-package
VERIFY_DIR=${WORK_DIR}/verify

mkdir -p "${SOURCE_DIR}" "${VERIFY_DIR}" "${OUTPUT_DIR}"

echo "Downloading pinned wiliwili FFmpeg recipe ${WILIWILI_COMMIT}"
curl --fail --location --retry 3 --output "${ARCHIVE}" \
    "https://github.com/xfangfang/wiliwili/archive/${WILIWILI_COMMIT}.tar.gz"
verify_sha256 "${WILIWILI_ARCHIVE_SHA256}" "${ARCHIVE}"
tar -xzf "${ARCHIVE}" -C "${SOURCE_DIR}"

cp -R \
    "${SOURCE_DIR}/wiliwili-${WILIWILI_COMMIT}/scripts/switch/ffmpeg" \
    "${RECIPE_DIR}"

verify_sha256 "${FFMPEG_PATCH_SHA256}" "${RECIPE_DIR}/ffmpeg.patch"
verify_sha256 "${NETWORK_PATCH_SHA256}" "${RECIPE_DIR}/network.patch"
verify_sha256 "${RANDOM_PATCH_SHA256}" "${SCRIPT_DIR}/ffmpeg_switch_random.patch"
cp "${SCRIPT_DIR}/ffmpeg_switch_random.patch" "${RECIPE_DIR}/ffmpeg_switch_random.patch"

awk \
    -v ffmpeg_sha="${FFMPEG_ARCHIVE_SHA256}" \
    -v ffmpeg_patch_sha="${FFMPEG_PATCH_SHA256}" \
    -v network_patch_sha="${NETWORK_PATCH_SHA256}" \
    -v random_patch_sha="${RANDOM_PATCH_SHA256}" '
BEGIN {
    checksum_index = 0
    in_checksums = 0
}
/^pkgrel=/ {
    print "pkgrel=3"
    next
}
/^source=\(/ {
    line = $0
    if (sub(/"network.patch"\)/, "\"network.patch\" \"ffmpeg_switch_random.patch\")", line) != 1) exit 2
    print line
    source_count++
    next
}
/^[[:space:]]*patch -Np1 -i "\$srcdir\/network.patch"/ {
    print
    print "  patch -Np1 -i \"$srcdir/ffmpeg_switch_random.patch\""
    prepare_count++
    next
}
/^sha256sums=\(/ {
    in_checksums = 1
    print
    next
}
in_checksums && /'"'"'SKIP'"'"'/ {
    checksum_index++
    if (checksum_index == 1) print "            '"'"'" ffmpeg_sha "'"'"'"
    if (checksum_index == 2) print "            '"'"'" ffmpeg_patch_sha "'"'"'"
    if (checksum_index == 3) print "            '"'"'" network_patch_sha "'"'"'"
    next
}
in_checksums && /^\)/ {
    in_checksums = 0
    print "            \"" random_patch_sha "\""
    print
    next
}
{
    line = $0
    sub(/--disable-muxers/, "--disable-muxers --enable-muxer=matroska", line)
    sub(/make -j\$\(nproc\)/, "make -j\"${NXCAST_FFMPEG_JOBS:-4}\"", line)
    print line
}
END {
    if (checksum_index != 3 || source_count != 1 || prepare_count != 1) exit 2
}
' "${RECIPE_DIR}/PKGBUILD" > "${RECIPE_DIR}/PKGBUILD.nxcast"
mv "${RECIPE_DIR}/PKGBUILD.nxcast" "${RECIPE_DIR}/PKGBUILD"

if [[ $(grep -Fc -- '--enable-muxer=matroska' "${RECIPE_DIR}/PKGBUILD") -ne 1 ]]; then
    echo "Failed to enable exactly one Matroska muxer in PKGBUILD." >&2
    exit 1
fi

if grep -Fq "'SKIP'" "${RECIPE_DIR}/PKGBUILD"; then
    echo "PKGBUILD still contains an unverified source checksum." >&2
    exit 1
fi

echo "Building ${PACKAGE_NAME} with ${NXCAST_FFMPEG_JOBS} jobs"
(
    cd "${RECIPE_DIR}"
    dkp-makepkg -Ccf --noconfirm
)

BUILT_PACKAGE=$(find "${RECIPE_DIR}" -maxdepth 1 -type f -name "${PACKAGE_NAME}" -print -quit)
if [[ -z ${BUILT_PACKAGE} ]]; then
    echo "Expected package was not produced: ${PACKAGE_NAME}" >&2
    exit 1
fi

tar -xf "${BUILT_PACKAGE}" -C "${VERIFY_DIR}"
PACKAGE_PREFIX="${VERIFY_DIR}/opt/devkitpro/portlibs/switch"
if [[ ! -d ${PACKAGE_PREFIX} ]]; then
    echo "Built package does not contain the expected Switch portlibs prefix." >&2
    exit 1
fi

"${VERIFY_SCRIPT}" "${PACKAGE_PREFIX}"

cp "${BUILT_PACKAGE}" "${OUTPUT_DIR}/${PACKAGE_NAME}"
echo "Package: ${OUTPUT_DIR}/${PACKAGE_NAME}"
