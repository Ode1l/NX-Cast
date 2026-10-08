# Switch FFmpeg And libmpv Toolchain

This guide documents the current practical route for `NX-Cast` media dependencies.

This is the maintained dependency installation recipe. For VS Code tasks,
uploading, tracing and publishing, use [developer-workflow.md](developer-workflow.md).
Ordinary users only need the [SD installation ZIP](install.md), not these packages.

## Goals

The toolchain must provide:

1. FFmpeg with network playback support.
2. `libmpv` with `hos-audio`.
3. `libmpv` render support for `deko3d`.
4. FFmpeg/mpv support for `nvtegra` hardware decode when available.
5. One reproducible setup for local builds, Docker, and GitHub Actions.

## Recommended Route

Use Wiliwili's prebuilt libuam and deko3d libmpv with NX-Cast's pinned FFmpeg.
Install NX-Cast FFmpeg directly; do not install Wiliwili's older FFmpeg first.
The pinned package includes the Matroska muxer, libnx-backed random bytes and
the nvtegra reference-frame fix needed for screen mirroring.

Only rebuild FFmpeg/mpv locally if:

1. A prebuilt package version is wrong.
2. You need to modify an FFmpeg patch.
3. You need to modify mpv `deko3d` or `hos-audio` behavior.
4. You are debugging a lower-level media stack bug.

Do not start by manually building upstream FFmpeg/mpv or collecting unrelated SwitchWave dependencies. That path is slower and easier to break.

## Current Package Set

The release path uses deko3d, hos-audio and nvtegra:

```text
libuam-f8c9eef01ffe06334d530393d636d69e2b52744b-1-any.pkg.tar.zst
switch-ffmpeg-7.1-4-any.pkg.tar.zst
switch-libmpv_deko3d-0.36.0-2-any.pkg.tar.zst
```

## Install Prebuilt Packages

Install devkitPro/devkitA64, libnx and the required Switch portlibs first.
The current container checks the following dependency set; see the repository
[Dockerfile](../Dockerfile) for the CI baseline. The local package commands below
are not CI instructions: CI reuses its provisioned toolchain image.

Run from the repository root in Bash. On **macOS**, initialize the SDK and select
the package manager (change DEVKITPRO if your installation is elsewhere):

```bash
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
source scripts/dev_environment.sh
nxcast_load_sdk
pkg=(sudo dkp-pacman)
```

On **Windows**, run the following instead in **devkitPro-compatible MSYS2 Bash**,
not PowerShell, Git Bash or WSL. DEVKITPRO must point to your installed SDK:

```bash
export DEVKITPRO="$(cygpath -u "${DEVKITPRO:?Set DEVKITPRO to your SDK directory}")"
source scripts/dev_environment.sh
nxcast_load_sdk
pkg=(pacman)
```

MSYS2 uses `pacman` without `sudo`. See the
[Windows setup](developer-workflow.md#windows) for custom paths and the VS Code
PowerShell adapter. Native Windows execution is still pending validation.

After choosing **one** platform setup above, run these shared Bash commands.
Install libuam first, then pinned FFmpeg, then deko3d libmpv:

```bash
"${pkg[@]}" -S --needed \
  switch-pkg-config switch-libjpeg-turbo switch-zlib switch-bzip2 \
  switch-libass switch-libfribidi switch-freetype switch-harfbuzz \
  switch-mbedtls switch-libsodium switch-liblua51 deko3d

base_url="https://github.com/xfangfang/wiliwili/releases/download/v0.1.0"
"${pkg[@]}" -U \
  "$base_url/libuam-f8c9eef01ffe06334d530393d636d69e2b52744b-1-any.pkg.tar.zst"
make install-airplay-ffmpeg
make verify-airplay-ffmpeg
"${pkg[@]}" -U \
  "$base_url/switch-libmpv_deko3d-0.36.0-2-any.pkg.tar.zst"
```

Do not install `switch-libmpv` and `switch-libmpv_deko3d` together. If an existing
package conflicts, inspect the package manager's message and its dependents;
do not bypass dependency checks to force installation.

## Automatic FFmpeg Preparation

The install target downloads `switch-ffmpeg-7.1-4-any.pkg.tar.zst` from the
`toolchain-ffmpeg-7.1-4` NX-Cast GitHub Release, verifies its pinned SHA-256,
and installs it under `$DEVKITPRO/portlibs/switch`. The pinned filename and hash
are maintained in [fetch_switch_ffmpeg_airplay.sh](../scripts/fetch_switch_ffmpeg_airplay.sh).
If that package version
and its required files are already installed, it returns without downloading,
requesting sudo, or reinstalling. It requests `sudo` only for the local
`dkp-pacman -U` operation on macOS; Windows MSYS2 uses `pacman` without sudo,
and root container builds invoke `dkp-pacman` directly.
The source recipe remains available
through `make build-airplay-ffmpeg` when maintaining the media toolchain.

Plain `make`, `dev-build`/`dev-rebuild`, both trace build/rebuild variants, and
`release-build` now run this preparation automatically before Make's media
dependency checks. VS Code tasks and nxlink builds inherit the same behavior.
Download/installation failures stop compilation. Clean, host tests and dry runs
do not automatically install anything. The package name is part of the existing
build configuration signature so a pinned revision change rebuilds old objects.

For a separately managed staging prefix, pass `NXCAST_AUTO_INSTALL_FFMPEG=0`
and provision that prefix yourself. Automatic installation only manages the
global devkitPro Switch package prefix.

Revision 4 fixes a long-term H.264 reference indexing error in nvtegra that
can crash screen mirroring. It keeps hardware decoding enabled and includes
the earlier Matroska and libnx random fixes. After updating the repository,
the next build automatically installs the pinned package if necessary before
rebuilding NX-Cast.

Matroska is compiled into target `libavformat.a`; it is not a separate runtime
plugin and nothing needs to be copied to the Switch SD card. End users receive
the linked capability inside `NX-Cast.nro`.

## Validate Installation

Optional manual diagnostics below assume the SDK environment has been loaded
as above. They do not add any new CI checks. Header and library checks:

```bash
prefix="$DEVKITPRO/portlibs/switch"
test -f "$prefix/include/mpv/client.h" && echo "mpv headers ok"
test -f "$prefix/include/mpv/render_dk3d.h" && echo "render_dk3d ok"
test -f "$prefix/lib/libmpv.a" && echo "libmpv ok"
test -f "$prefix/include/libavutil/hwcontext_nvtegra.h" && echo "nvtegra hwcontext ok"
```

Static link check:

```bash
PKG_CONFIG_PATH="$prefix/lib/pkgconfig" pkg-config --static --libs mpv
```

The output should include `-ldeko3d` and `-luam` for the release path.

Run the same AirPlay media capability gate used by release builds and CI:

```bash
make verify-airplay-ffmpeg
```

Symbol/string checks:

```bash
strings "$prefix/lib/libmpv.a" | rg "deko3d|hos|nvtegra"
strings "$prefix/lib/libavcodec.a" | rg "nvtegra|configuration|license"
aarch64-none-elf-nm -g --defined-only \
  "$prefix/lib/libavcodec.a" | \
  rg "ff_alac_decoder|ff_h264_parser"
aarch64-none-elf-nm -g --defined-only \
  "$prefix/lib/libavformat.a" | \
  rg "ff_matroska_muxer"
```

## Build NX-Cast

Use the [developer workflow](developer-workflow.md) for normal builds, Full Trace,
uploads, and local release compilation followed by SD ZIP packaging. It also
documents publication through GitHub; creating a local ZIP does not publish it.

The strict release target rejects a missing libmpv/deko3d backend, AirPlay
crypto support, or an FFmpeg package without ALAC, the H.264 parser, and the
Matroska muxer. The packaging script also rejects an SD zip that does not
contain exactly `switch/NX-Cast/NX-Cast.nro`.

## Docker And GitHub Actions

The repository `Dockerfile` installs the same prebuilt Wiliwili baseline, then
downloads the pinned NX-Cast FFmpeg Release asset, installs it with
`dkp-pacman -U`, and verifies the installed target prefix.
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

Use `make build-airplay-ffmpeg` to produce the supported NX-Cast package from
source without installing it, or `make install-airplay-ffmpeg` to download and
install the published prebuilt package. To install your own freshly built
package, use the `pkg` array selected for your platform above:

```bash
"${pkg[@]}" -U artifacts/toolchain/ffmpeg/switch-ffmpeg-7.1-4-any.pkg.tar.zst
make verify-airplay-ffmpeg
```

Toolchain packages are cached under `artifacts/toolchain/ffmpeg`, outside the
application's `build` directory, so rebuilding NX-Cast does not delete them.
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
3. Build `switch-libmpv_deko3d`.
4. Install the generated packages with your platform's package manager.
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

## Historical Alternatives

Earlier experiments used Wiliwili's `switch-ffmpeg-7.1-1` and the OpenGL
`switch-libmpv-0.36.0-3` package. These are not the current installation route:
that FFmpeg lacks the required muxer, and OpenGL libmpv does not provide the
release deko3d render path. Upstream FFmpeg/mpv and full SwitchWave dependency
builds were also explored; do not substitute them for the pinned package set.
