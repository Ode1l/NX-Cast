# libmpv Dependencies

This document summarizes the current dependency route for `libmpv`, `FFmpeg`, `deko3d`, `hos-audio`, and `nvtegra`.

## Current Recommendation

Use the `wiliwili` prebuilt media packages for the current release path:

1. `libuam`
2. `switch-ffmpeg`
3. `switch-libmpv_deko3d`

This is the same route used by the project Dockerfile and GitHub Actions.

## Why Not Plain devkitPro Packages

Plain devkitPro packages remain useful as a fallback, but they are not the current release baseline because the project needs:

1. Network playback through FFmpeg.
2. `hos-audio`.
3. `deko3d` render support.
4. `nvtegra` hardware decode support when available.

Use plain packages only for minimal protocol/player debugging.

## Current Project State

`NX-Cast` currently has:

1. `deko3d` render wiring.
2. Local player overlay and input controls.
3. `show_osd` bridge.
4. Runtime preference for `hwdec=nvtegra`.
5. Process-volume handling through `aud:a` when available.

The parts that still depend on installed packages are:

1. Whether `mpv/render_dk3d.h` exists.
2. Whether `libmpv` is really a `deko3d` build.
3. Whether `FFmpeg` exposes `hwcontext_nvtegra`.

## Validation

Useful local checks:

```bash
test -f /opt/devkitpro/portlibs/switch/include/mpv/client.h
test -f /opt/devkitpro/portlibs/switch/include/mpv/render_dk3d.h
test -f /opt/devkitpro/portlibs/switch/lib/libmpv.a
pkg-config --static --libs mpv
strings /opt/devkitpro/portlibs/switch/lib/libmpv.a | rg "deko3d|hos|nvtegra"
```

Strict project build:

```bash
source /opt/devkitpro/switchvars.sh
make NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 -j2
NXCAST_MIN_NRO_SIZE=5000000 ./scripts/package_release.sh
```

## Full Toolchain Guide

For installation and rebuild details, see [ffmpeg-mpv-toolchain.md](ffmpeg-mpv-toolchain.md).
