# Switch FFmpeg And libmpv Toolchain

This guide documents the current practical route for `NX-Cast` media dependencies.

The project previously explored several paths, including upstream FFmpeg/mpv builds and full SwitchWave-style dependency builds. The current shortest working path is to reuse the `wiliwili` Switch media packages and build scripts.

## Goals

The toolchain must provide:

1. FFmpeg with network playback support.
2. `libmpv` with `hos-audio`.
3. `libmpv` render support for `deko3d`.
4. FFmpeg/mpv support for `nvtegra` hardware decode when available.
5. One reproducible setup for local builds, Docker, and GitHub Actions.

## Recommended Route

Use the prebuilt `wiliwili` media packages as the baseline. NX-Cast then rebuilds
the same pinned FFmpeg recipe with one additional Matroska muxer for the
experimental AirPlay bridge.

Only rebuild FFmpeg/mpv locally if:

1. A prebuilt package version is wrong.
2. You need to modify an FFmpeg patch.
3. You need to modify mpv `deko3d` or `hos-audio` behavior.
4. You are debugging a lower-level media stack bug.

Do not start by manually building upstream FFmpeg/mpv or collecting unrelated SwitchWave dependencies. That path is slower and easier to break.

## Package Variants

### OpenGL Variant

Characteristics:

1. Easiest to install.
2. Fixes network-video support compared with some plain devkitPro media packages.
3. Does not provide the current release `deko3d` render path.

Typical packages:

```text
switch-ffmpeg-7.1-1-any.pkg.tar.zst
switch-libmpv-0.36.0-3-any.pkg.tar.zst
```

### deko3d Variant

Characteristics:

1. Current `NX-Cast` release path.
2. Provides `deko3d` rendering.
3. Provides `hos-audio`.
4. Best fit for hardware decode work.

Typical packages:

```text
libuam-f8c9eef01ffe06334d530393d636d69e2b52744b-1-any.pkg.tar.zst
switch-ffmpeg-7.1-1-any.pkg.tar.zst
switch-libmpv_deko3d-0.36.0-2-any.pkg.tar.zst
```

## Install Prebuilt Packages

Prepare the devkitPro environment:

```bash
source /opt/devkitpro/switchvars.sh
```

Install the current recommended `deko3d` package set:

```bash
base_url="https://github.com/xfangfang/wiliwili/releases/download/v0.1.0"
sudo dkp-pacman -U \
  "$base_url/libuam-f8c9eef01ffe06334d530393d636d69e2b52744b-1-any.pkg.tar.zst" \
  "$base_url/switch-ffmpeg-7.1-1-any.pkg.tar.zst" \
  "$base_url/switch-libmpv_deko3d-0.36.0-2-any.pkg.tar.zst"
```

Do not install `switch-libmpv` and `switch-libmpv_deko3d` at the same time. Use `switch-libmpv_deko3d` for the release path.

The prebuilt `switch-ffmpeg-7.1-1` package disables all muxers. It is sufficient
for DLNA and IPTV playback, but it cannot create the H.264/AAC/ALAC Matroska
stream used by the AirPlay bridge. Build and install the NX-Cast package after
the baseline packages:

```bash
source /opt/devkitpro/switchvars.sh
make NXCAST_FFMPEG_JOBS=4 install-airplay-ffmpeg
make verify-airplay-ffmpeg
```

The install target pins the wiliwili recipe and FFmpeg source revisions,
verifies their SHA-256 checksums, enables only `matroska`, checks the generated
static archives, and installs the complete package globally under
`/opt/devkitpro/portlibs/switch`. It builds as the current user and requests
`sudo` only for the local `dkp-pacman -U` operation. Do not run the package
build script itself with `sudo`.

Matroska is compiled into target `libavformat.a`; it is not a separate runtime
plugin and nothing needs to be copied to the Switch SD card. End users receive
the linked capability inside `NX-Cast.nro`.

## Validate Installation

Header and library checks:

```bash
test -f /opt/devkitpro/portlibs/switch/include/mpv/client.h && echo "mpv headers ok"
test -f /opt/devkitpro/portlibs/switch/include/mpv/render_dk3d.h && echo "render_dk3d ok"
test -f /opt/devkitpro/portlibs/switch/lib/libmpv.a && echo "libmpv ok"
test -f /opt/devkitpro/portlibs/switch/include/libavutil/hwcontext_nvtegra.h && echo "nvtegra hwcontext ok"
```

Static link check:

```bash
PKG_CONFIG_PATH=/opt/devkitpro/portlibs/switch/lib/pkgconfig pkg-config --static --libs mpv
```

The output should include `-ldeko3d` and `-luam` for the release path.

Run the same AirPlay media capability gate used by release builds and CI:

```bash
make verify-airplay-ffmpeg
```

Symbol/string checks:

```bash
strings /opt/devkitpro/portlibs/switch/lib/libmpv.a | rg "deko3d|hos|nvtegra"
strings /opt/devkitpro/portlibs/switch/lib/libavcodec.a | rg "nvtegra|configuration|license"
aarch64-none-elf-nm -g --defined-only \
  /opt/devkitpro/portlibs/switch/lib/libavcodec.a | \
  rg "ff_alac_decoder|ff_h264_parser"
aarch64-none-elf-nm -g --defined-only \
  /opt/devkitpro/portlibs/switch/lib/libavformat.a | \
  rg "ff_matroska_muxer"
```

## Build NX-Cast

```bash
source /opt/devkitpro/switchvars.sh
make clean
make RELEASE_JOBS=2 release-build
NXCAST_MIN_NRO_SIZE=5000000 ./scripts/package_release.sh
```

The strict release target rejects a missing libmpv/deko3d backend, AirPlay
crypto support, or an FFmpeg package without ALAC, the H.264 parser, and the
Matroska muxer. The packaging script also rejects an SD zip that does not
contain exactly `switch/NX-Cast/NX-Cast.nro`.

## Docker And GitHub Actions

The repository `Dockerfile` installs the same prebuilt package set, rebuilds
the pinned FFmpeg package as a non-root user, globally installs it into the
image with a local `dkp-pacman -U`, and verifies the installed target prefix.
It does not use `dkp-pacman -S` to access devkitPro repositories during CI.
GitHub Actions uses that Dockerfile and repeats the verifier before compiling
both continuous builds and tagged releases.

Local Docker release build:

```bash
./scripts/docker_build_release.sh
```

Expected outputs:

```text
dist/NX-Cast.nro
dist/NX-Cast-sdmc.zip
```

If local and CI behavior differs, first check:

1. Which `switch-libmpv` package is installed.
2. Whether `pkg-config --static --libs mpv` includes `-ldeko3d -luam`.
3. Whether `render_dk3d.h` exists.
4. Whether the generated NRO size is plausible.

## Rebuilding Packages Locally

Use `make build-airplay-ffmpeg` to produce the supported NX-Cast package without
installing it, or `make install-airplay-ffmpeg` for the normal global install.
Only edit or invoke the upstream package recipes directly when developing the
media toolchain itself.

The useful references are the `wiliwili-dev` package scripts:

```text
scripts/switch/ffmpeg/PKGBUILD
scripts/switch/mpv/PKGBUILD
scripts/switch/mpv_deko3d/PKGBUILD
scripts/build_switch_deko3d.sh
```

Expected package order:

1. Build or install low-level dependencies such as `libuam` when required.
2. Build `switch-ffmpeg`, keeping all muxers disabled except Matroska.
3. Build `switch-libmpv` or `switch-libmpv_deko3d`.
4. Install the generated packages with `dkp-pacman -U`.
5. Rebuild `NX-Cast` with strict media requirements.

Do not mix a locally rebuilt FFmpeg with an incompatible prebuilt mpv package unless you know the ABI and package configuration match.

## Application Wiring Reference

The application-side ideas to study from `wiliwili` are:

1. Include `mpv/render_dk3d.h`.
2. Initialize `mpv_deko3d_init_params`.
3. Attach `MPV_RENDER_API_TYPE_DEKO3D`.
4. Use `vo=libmpv`.
5. Use `ao=hos`.
6. Set `hwdec` as a runtime mpv option rather than writing a decoder in the app.

## Current Recommendation

For `NX-Cast`, the practical route remains:

1. Install the `wiliwili` prebuilt `deko3d` package set.
2. Run `make install-airplay-ffmpeg` to install the pinned NX-Cast FFmpeg package with Matroska output.
3. Build `NX-Cast` against the system `portlibs`.
4. Verify `render_dk3d.h`, `libmpv.a`, `hwcontext_nvtegra.h`, and the required FFmpeg symbols.
5. Use Docker when reproducing CI.
6. Rebuild mpv only when changing the renderer or audio backend itself.
