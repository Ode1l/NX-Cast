#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

IMAGE="${NXCAST_DOCKER_IMAGE:-nx-cast-build:local}"
BASE_IMAGE="${NXCAST_BASE_IMAGE:-devkitpro/devkita64:latest}"
JOBS="${JOBS:-}"
NXCAST_MIN_NRO_SIZE="${NXCAST_MIN_NRO_SIZE:-5000000}"

docker build --pull \
    --build-arg BASE_IMAGE="${BASE_IMAGE}" \
    -t "${IMAGE}" \
    "${ROOT_DIR}"

docker run --rm \
    -e DEVKITPRO=/opt/devkitpro \
    -e JOBS="${JOBS}" \
    -e NXCAST_MIN_NRO_SIZE="${NXCAST_MIN_NRO_SIZE}" \
    -v "${ROOT_DIR}:/workspace" \
    -w /workspace \
    "${IMAGE}" \
    bash -lc 'set -euo pipefail; jobs="${JOBS:-$(nproc)}"; /usr/local/bin/verify_switch_ffmpeg_airplay.sh /opt/devkitpro/portlibs/switch; host_pc="/usr/lib/$(gcc -print-multiarch)/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig"; PKG_CONFIG_PATH="${host_pc}" make test-airplay; make RELEASE_JOBS="${jobs}" release-build; ./scripts/package_release.sh'
