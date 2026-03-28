#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

NO_CLEAN="${NO_CLEAN:-0}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

if [[ "${NO_CLEAN}" == "1" ]]; then
  BUILD_CMD="make -j${JOBS}"
else
  BUILD_CMD="make clean && make -j${JOBS}"
fi

if command -v docker >/dev/null 2>&1; then
  :
else
  echo "[docker-build] docker not found. Please install Docker first." >&2
  exit 1
fi

if docker compose version >/dev/null 2>&1; then
  echo "[docker-build] using docker compose"
  (
    cd "${PROJECT_ROOT}"
    docker compose build nx-cast-build
    docker compose run --rm nx-cast-build bash -lc "${BUILD_CMD}"
  )
  exit 0
fi

IMAGE_NAME="${IMAGE_NAME:-nx-cast-build:local}"
echo "[docker-build] docker compose not found, using docker run"
(
  cd "${PROJECT_ROOT}"
  docker build -t "${IMAGE_NAME}" .
  docker run --rm \
    -e DEVKITPRO=/opt/devkitpro \
    -v "${PROJECT_ROOT}:/workspace" \
    -w /workspace \
    "${IMAGE_NAME}" \
    bash -lc "${BUILD_CMD}"
)
