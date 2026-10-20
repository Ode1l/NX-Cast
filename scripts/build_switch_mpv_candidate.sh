#!/usr/bin/env bash
# Isolated candidate build: never installs into the active devkitPro prefix.
set -eo pipefail

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUTPUT_DIR=${1:-"${ROOT_DIR}/artifacts/toolchain/mpv-candidate"}
COMMIT=67c2e3f28be29a380a7eccdfdf59694bcd3ad417
ARCHIVE_SHA256=e5b04453cfe3d4fd1a323be2d0b07c3f4689b92c11352f33783a8e6c13af506c
PYTHON=${PYTHON:-python3}
JOBS=${JOBS:-4}
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}

case "$JOBS" in ''|*[!0-9]*|0) echo 'JOBS must be a positive integer' >&2; exit 1;; esac
for tool in curl tar patch shasum pkg-config "$PYTHON"; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 1; }
done
# This pinned waf-based fork requires distutils (or setuptools' compatibility module).
"$PYTHON" -c 'from distutils.version import StrictVersion' || {
    echo 'Use Python 3.11 or older, or install setuptools into the selected Python environment.' >&2
    exit 1
}
test -f "$DEVKITPRO/switchvars.sh"
source "$DEVKITPRO/switchvars.sh"
if [[ -n ${NXCAST_PORTLIBS_PREFIX:-} ]]; then
    # An isolated sysroot lets us test newly rebuilt FFmpeg without installation.
    CPPFLAGS=${CPPFLAGS//"$PORTLIBS_PREFIX"/"$NXCAST_PORTLIBS_PREFIX"}
    LDFLAGS=${LDFLAGS//"$PORTLIBS_PREFIX"/"$NXCAST_PORTLIBS_PREFIX"}
    export PORTLIBS_PREFIX=$NXCAST_PORTLIBS_PREFIX
fi
set -u
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR=$(cd "$OUTPUT_DIR" && pwd)
WORK=$(mktemp -d "$OUTPUT_DIR/work.XXXXXX")
echo "Candidate sources and logs: $WORK"
curl -fL --retry 3 "https://codeload.github.com/averne/mpv/tar.gz/$COMMIT" -o "$WORK/mpv.tar.gz"
printf '%s  %s\n' "$ARCHIVE_SHA256" "$WORK/mpv.tar.gz" | shasum -a 256 -c -
tar -xf "$WORK/mpv.tar.gz" -C "$WORK"
cd "$WORK/mpv-$COMMIT"
patch -p1 < "$ROOT_DIR/scripts/mpv_switch_cross.patch"
"$PYTHON" bootstrap.py 2>&1 | tee "$WORK/bootstrap.log"
export CFLAGS="$CFLAGS -D_POSIX_VERSION=200809L -DNDEBUG -I$PWD/osdep/switch"
export PKG_CONFIG_LIBDIR="$PORTLIBS_PREFIX/lib/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"
# The SDK wrapper resets PKG_CONFIG_LIBDIR to the system prefix.
export PKG_CONFIG=$(command -v pkg-config)
export TARGET=aarch64-none-elf
export LIBDIR="$PORTLIBS_PREFIX/lib"
"$PYTHON" waf configure --prefix="$PORTLIBS_PREFIX" \
    --disable-libmpv-shared --enable-libmpv-static --enable-static-build --disable-cplayer \
    --disable-sdl2 --disable-gl --disable-plain-gl --enable-lua \
    --enable-deko3d --enable-hos --enable-hos-audio --disable-iconv \
    --disable-jpeg --disable-libavdevice --disable-debug-build \
    --disable-manpage-build --disable-libplacebo 2>&1 | tee "$WORK/configure.log"
# Match the upstream Switch recipe: use dummy subprocess handling on Horizon.
sed 's/^#define HAVE_POSIX 1$/#define HAVE_POSIX 0/' build/config.h > build/config.h.tmp
mv build/config.h.tmp build/config.h
"$PYTHON" waf build -j"$JOBS" 2>&1 | tee "$WORK/build.log"
"$PYTHON" waf install --destdir="$WORK/stage" 2>&1 | tee "$WORK/install.log"
test -f "$WORK/stage$PORTLIBS_PREFIX/lib/libmpv.a"
test -f "$WORK/stage$PORTLIBS_PREFIX/include/mpv/render_dk3d.h"
mkdir -p "$WORK/stage$PORTLIBS_PREFIX/share/licenses/mpv-candidate"
cp LICENSE.GPL LICENSE.LGPL "$WORK/stage$PORTLIBS_PREFIX/share/licenses/mpv-candidate/"
cp "$ROOT_DIR/scripts/mpv_switch_cross.patch" "$WORK/stage$PORTLIBS_PREFIX/share/licenses/mpv-candidate/"
{
    printf 'source=https://github.com/averne/mpv\ncommit=%s\narchive_sha256=%s\n' "$COMMIT" "$ARCHIVE_SHA256"
    "$CC" --version
    "$PKG_CONFIG" --modversion libavcodec libavformat libavutil
} > "$WORK/stage/mpv-candidate-build.txt"
tar -czf "$WORK/mpv-candidate.tar.gz" -C "$WORK/stage" .
echo "Candidate archive: $WORK/mpv-candidate.tar.gz"
echo 'Not installed. Requires application linking and hardware validation before release use.'
