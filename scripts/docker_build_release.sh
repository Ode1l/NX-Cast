#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

IMAGE="${NXCAST_DOCKER_IMAGE:-nx-cast-build:local}"
NXCAST_MPV_VARIANT="${NXCAST_MPV_VARIANT:-deko3d}"
WILIWILI_RELEASE="${WILIWILI_RELEASE:-v0.1.0}"
LIBUAM_PKG="${LIBUAM_PKG:-libuam-f8c9eef01ffe06334d530393d636d69e2b52744b-1-any.pkg.tar.zst}"
SWITCH_FFMPEG_PKG="${SWITCH_FFMPEG_PKG:-switch-ffmpeg-7.1-1-any.pkg.tar.zst}"
SWITCH_LIBMPV_PKG="${SWITCH_LIBMPV_PKG:-switch-libmpv_deko3d-0.36.0-2-any.pkg.tar.zst}"
JOBS="${JOBS:-}"
NXCAST_MIN_NRO_SIZE="${NXCAST_MIN_NRO_SIZE:-5000000}"

docker build \
    --build-arg NXCAST_MPV_VARIANT="${NXCAST_MPV_VARIANT}" \
    --build-arg WILIWILI_RELEASE="${WILIWILI_RELEASE}" \
    --build-arg LIBUAM_PKG="${LIBUAM_PKG}" \
    --build-arg SWITCH_FFMPEG_PKG="${SWITCH_FFMPEG_PKG}" \
    --build-arg SWITCH_LIBMPV_PKG="${SWITCH_LIBMPV_PKG}" \
    -t "${IMAGE}" \
    "${ROOT_DIR}"

docker run --rm \
    -e DEVKITPRO=/opt/devkitpro \
    -e JOBS="${JOBS}" \
    -e NXCAST_MIN_NRO_SIZE="${NXCAST_MIN_NRO_SIZE}" \
    -v "${ROOT_DIR}:/workspace" \
    -w /workspace \
    "${IMAGE}" \
    bash -lc 'set -euo pipefail; jobs="${JOBS:-$(nproc)}"; make clean && make NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 -j"${jobs}" && ./scripts/package_release.sh'
