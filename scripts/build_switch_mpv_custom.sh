#!/usr/bin/env bash
set -euo pipefail

if [[ -f /opt/devkitpro/switchvars.sh ]]; then
  # shellcheck disable=SC1091
  source /opt/devkitpro/switchvars.sh
fi

: "${PORTLIBS_PREFIX:?PORTLIBS_PREFIX is not set}"

REPO_URL="${REPO_URL:-https://github.com/averne/mpv.git}"
WORK_DIR="${WORK_DIR:-$HOME/src/mpv-switch}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

if ! command -v git >/dev/null 2>&1; then
  echo "[build-switch-mpv-custom] missing git" >&2
  exit 1
fi

if [[ ! -d "$WORK_DIR/.git" ]]; then
  mkdir -p "$(dirname "$WORK_DIR")"
  git clone "$REPO_URL" "$WORK_DIR"
fi

cd "$WORK_DIR"
git fetch --all --tags --prune

./bootstrap.py
CFLAGS="${CFLAGS:-} -D_POSIX_VERSION=200809L -I./osdep/switch" \
TARGET=aarch64-none-elf \
./waf configure \
  --prefix="$PORTLIBS_PREFIX" \
  --enable-libmpv-static \
  --disable-libmpv-shared \
  --disable-manpage-build \
  --disable-cplayer \
  --disable-iconv \
  --disable-lua \
  --disable-sdl2 \
  --disable-gl \
  --disable-plain-gl \
  --enable-hos-audio \
  --enable-deko3d

if [[ -f build/config.h ]]; then
  perl -0pi -e 's/#define HAVE_POSIX 1/#define HAVE_POSIX 0/' build/config.h
fi

./waf -j"$JOBS"
./waf install

echo "[build-switch-mpv-custom] verifying libmpv install"
test -f "$PORTLIBS_PREFIX/include/mpv/client.h"
test -f "$PORTLIBS_PREFIX/lib/libmpv.a"
