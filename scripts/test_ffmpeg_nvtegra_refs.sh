#!/usr/bin/env bash
set -euo pipefail

SOURCE_FILE=${1:?Pass the patched libavcodec/nvtegra_h264.c path}
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/nxcast-nvtegra-test.XXXXXX")
trap 'rm -rf -- "${WORK_DIR}"' EXIT

awk '
/Build concatenated list of references/ { copying = 1; next }
copying && /Add all frames with an already allocated DPB index/ { copying = 0; found = 1 }
copying { print }
END { if (!found) exit 1 }
' "${SOURCE_FILE}" > "${WORK_DIR}/refs.inc"

"${HOST_CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -DNXCAST_REF_LIST_FILE="\"${WORK_DIR}/refs.inc\"" \
    "${SCRIPT_DIR}/test_ffmpeg_nvtegra_refs.c" -o "${WORK_DIR}/test"
"${WORK_DIR}/test"
