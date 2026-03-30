#!/usr/bin/env bash
set -euo pipefail

if [[ -f /opt/devkitpro/switchvars.sh ]]; then
  # shellcheck disable=SC1091
  source /opt/devkitpro/switchvars.sh
fi

: "${DEVKITPRO:?DEVKITPRO is not set}"
: "${PORTLIBS_PREFIX:?PORTLIBS_PREFIX is not set}"

REPO_URL="${REPO_URL:-https://github.com/averne/FFmpeg.git}"
REPO_BRANCH="${REPO_BRANCH:-nvtegra}"
WORK_DIR="${WORK_DIR:-$HOME/src/ffmpeg-switch}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"
INSTALL_TARGETS=(install-libs install-headers)

if ! command -v aarch64-none-elf-gcc >/dev/null 2>&1; then
  echo "[build-switch-ffmpeg-https] missing aarch64-none-elf-gcc" >&2
  exit 1
fi

if ! command -v git >/dev/null 2>&1; then
  echo "[build-switch-ffmpeg-https] missing git" >&2
  exit 1
fi

if [[ ! -d "$WORK_DIR/.git" ]]; then
  mkdir -p "$(dirname "$WORK_DIR")"
  git clone "$REPO_URL" "$WORK_DIR"
fi

cd "$WORK_DIR"
git fetch --all --tags --prune
git checkout "$REPO_BRANCH"

make distclean >/dev/null 2>&1 || true

./configure \
  --prefix="$PORTLIBS_PREFIX" \
  --enable-gpl \
  --disable-shared \
  --enable-static \
  --cross-prefix=aarch64-none-elf- \
  --enable-cross-compile \
  --arch=aarch64 \
  --cpu=cortex-a57 \
  --target-os=horizon \
  --enable-pic \
  --extra-cflags='-D__SWITCH__ -D_GNU_SOURCE -D_POSIX_VERSION=200809L -Dtimegm=mktime -O2 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec' \
  --extra-cxxflags='-D__SWITCH__ -D_GNU_SOURCE -D_POSIX_VERSION=200809L -Dtimegm=mktime -O2 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec' \
  --extra-ldflags="-fPIE -L${PORTLIBS_PREFIX}/lib -L${DEVKITPRO}/libnx/lib" \
  --disable-runtime-cpudetect \
  --disable-programs \
  --disable-debug \
  --disable-doc \
  --disable-autodetect \
  --enable-asm \
  --enable-neon \
  --enable-swscale \
  --enable-swresample \
  --enable-network \
  --disable-protocols \
  --enable-protocol='file,crypto,http,https,tcp,tls,udp,hls' \
  --enable-demuxer=hls \
  --enable-zlib \
  --enable-bzlib \
  --enable-mbedtls \
  --enable-version3

make -j"$JOBS"

if [[ "${SKIP_INSTALL:-0}" == "1" ]]; then
  echo "[build-switch-ffmpeg-https] build complete; install skipped (SKIP_INSTALL=1)"
else
  if [[ -w "$PORTLIBS_PREFIX/lib" && -w "$PORTLIBS_PREFIX/include" && -w "$PORTLIBS_PREFIX/lib/pkgconfig" ]]; then
    make "${INSTALL_TARGETS[@]}"
  elif command -v sudo >/dev/null 2>&1 && [[ -t 0 && -t 1 ]]; then
    echo "[build-switch-ffmpeg-https] installing into $PORTLIBS_PREFIX with sudo"
    sudo env "PATH=$PATH" make "${INSTALL_TARGETS[@]}"
  else
    echo "[build-switch-ffmpeg-https] build succeeded, but install needs write access to $PORTLIBS_PREFIX" >&2
    echo "[build-switch-ffmpeg-https] rerun install manually:" >&2
    echo "  cd \"$WORK_DIR\" && sudo env \"PATH=$PATH\" make ${INSTALL_TARGETS[*]}" >&2
    exit 2
  fi
fi

echo "[build-switch-ffmpeg-https] verifying installed protocols"
strings "$PORTLIBS_PREFIX/lib/libavformat.a" | rg 'ff_https_protocol|ff_hls_demuxer|ff_http_protocol|ff_tls_protocol|https protocol not found' || true
