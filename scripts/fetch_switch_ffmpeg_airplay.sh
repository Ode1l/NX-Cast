#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUTPUT_DIR=${1:-"${ROOT_DIR}/build/toolchain/ffmpeg"}
PACKAGE_NAME=switch-ffmpeg-7.1-3-any.pkg.tar.zst
PACKAGE_SHA256=fc47e883da3693847e7a845e9de3b235c3449bd9103a046bdb200faf61f48b70
PACKAGE_URL=${NXCAST_FFMPEG_PACKAGE_URL:-"https://github.com/Ode1l/NX-Cast/releases/download/toolchain-ffmpeg-7.1-3/${PACKAGE_NAME}"}
PACKAGE_PATH="${OUTPUT_DIR}/${PACKAGE_NAME}"

for command in curl mktemp mkdir mv; do
    if ! command -v "${command}" >/dev/null 2>&1; then
        echo "Required command not found: ${command}" >&2
        exit 1
    fi
done

if command -v sha256sum >/dev/null 2>&1; then
    HASH_COMMAND=sha256sum
elif command -v shasum >/dev/null 2>&1; then
    HASH_COMMAND=shasum
else
    echo "sha256sum or shasum is required." >&2
    exit 1
fi

verify_package() {
    local actual
    if [[ ${HASH_COMMAND} == shasum ]]; then
        actual=$(shasum -a 256 "$1")
    else
        actual=$(sha256sum "$1")
    fi
    [[ ${actual%% *} == "${PACKAGE_SHA256}" ]]
}

mkdir -p "${OUTPUT_DIR}"
if [[ -f ${PACKAGE_PATH} ]] && verify_package "${PACKAGE_PATH}"; then
    echo "Verified cached Switch FFmpeg package: ${PACKAGE_PATH}"
    exit 0
fi

TEMP_PATH=$(mktemp "${OUTPUT_DIR}/.${PACKAGE_NAME}.XXXXXX")
trap 'rm -f -- "${TEMP_PATH}"' EXIT

echo "Downloading Switch FFmpeg package: ${PACKAGE_URL}"
curl --fail --location --retry 3 --output "${TEMP_PATH}" "${PACKAGE_URL}"
if ! verify_package "${TEMP_PATH}"; then
    echo "SHA-256 mismatch for ${PACKAGE_URL}; package was not installed or cached." >&2
    exit 1
fi

mv -f -- "${TEMP_PATH}" "${PACKAGE_PATH}"
echo "Verified Switch FFmpeg package: ${PACKAGE_PATH}"
