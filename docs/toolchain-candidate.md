# Latest-base media toolchain candidate

This is an opt-in experiment, not the production Release environment. The
production `Dockerfile`, `toolchain.yml`, `build.yml`, and `release.yml` are
unchanged. Do not promote the candidate on compilation results alone.

## Sources

- FFmpeg: existing `scripts/build_switch_ffmpeg_airplay.sh`, with the same
  Wiliwili recipe, Matroska muxer, libnx random, and nvtegra long-reference fixes.
- mpv: `averne/mpv` commit `67c2e3f28be29a380a7eccdfdf59694bcd3ad417`, with
  an archive SHA-256 check and `scripts/mpv_switch_cross.patch`.
- libuam in the candidate container: commit
  `f8c9eef01ffe06334d530393d636d69e2b52744b`, rebuilt from a verified archive.

The Wiliwili `mpv_deko3d` recipe references a missing `mpv.patch`. The candidate
therefore uses a different, auditable source implementation, not a proven
reconstruction of the old binary. The deko3d render header matches the old
package; that does not prove audio or rendering behavior is identical.
Do not use averne's `envideo` branch: it expects `AV_PIX_FMT_ENVIDEO`, whereas
our FFmpeg exposes nvtegra. No decoder renaming workaround is applied.

The only mpv source patch sets the cross-compilation target to Horizon before
waf detects the compiler. Otherwise a macOS host can select Darwin timer and
semaphore sources. Configuration follows the Switch recipe, enables static
dependency discovery, and disables the unrelated libplacebo backend.

## Isolated local mpv build

```sh
PYTHON=/usr/bin/python3 bash scripts/build_switch_mpv_candidate.sh
```

On other hosts select a Python with `distutils`, either Python <=3.11 or a
Python environment with setuptools' compatibility module. The script fails
early with instructions if it is unavailable. `JOBS` defaults to 4.

The script never installs libraries. It prints a unique work directory under
`artifacts/toolchain/mpv-candidate/`, keeping configure/build/install logs and
a staged archive. It requires existing Switch FFmpeg/deko3d/libuam dependencies.
`NXCAST_PORTLIBS_PREFIX` selects an isolated sysroot instead of the system
portlibs; its pkg-config files must refer to that sysroot. This is useful when
testing a freshly rebuilt FFmpeg package without modifying `/opt/devkitpro`.

## Candidate container and Actions

```sh
docker build --pull -f Dockerfile.candidate -t nxcast-toolchain-candidate .
```

`Dockerfile.candidate` defaults to the official `devkitpro/devkita64:latest`.
It rebuilds libuam, FFmpeg, then mpv. It does not download a precompiled media
package. Application auto-install of FFmpeg is disabled inside the image so
the freshly rebuilt library is not replaced with the old Release asset.

The **Candidate latest media toolchain** workflow resolves latest's
digest and combines it with the recipe hash. The digest is used for that build
to avoid a tag moving mid-build; it is not a permanently pinned base version.
Unchanged inputs reuse the image. Changed base or recipes rebuild the image.
Images use a separate `-toolchain-candidate` GHCR package. This workflow builds
the application and uploads an Actions artifact, never a GitHub Release.

The workflow must be available on the default branch before GitHub exposes its
manual dispatch UI. Adding this file locally does not run or publish anything.
Once available, select the intended candidate branch in the manual run dialog.
Before merging to main, pushes changing candidate recipes on
`codex/toolchain-candidate` trigger the same workflow automatically. This allows
cloud validation without enabling a production build or publishing a release.

## Verification and promotion

On 2026-10-18, the local cross-build passed with devkitA64 r30-1 (GCC 16.1.0),
libnx 4.12.0-1, and deko3d 0.5.0-1. FFmpeg 7.1-4 and candidate mpv were both
rebuilt. The application link map confirms the isolated media libraries were
used. On 2026-10-20 libuam was also rebuilt from the pinned source using GCC
16.1.0, then installed only into the isolated sysroot. NX-Cast linked against
all three rebuilt dependencies successfully. No production libraries were
installed or replaced.

The local libuam build required Bison 3.8.2 instead of macOS's old system
Bison. Meson 1.12.1, Ninja 1.13.2, Mako and setuptools were installed in a
project-local Python environment under `artifacts/toolchain/candidate-research/`.
No upstream libuam source changes were necessary. The candidate Linux image
already installs Bison via apt, but its complete build remains unverified.

Local outputs:

- `artifacts/toolchain/NX-Cast-source-candidate.nro`
- `artifacts/toolchain/NX-Cast-source-candidate.elf` (keep for matching crash reports)
- NRO SHA-256: `455d65295c176d448329af5d20b6aed3b2a5d89fe772e20fc7721e4c3026c683`
- ELF SHA-256: `632393c8517ac3cb2840fceb2ed3cf60c7e494f4f2fb0f89d38ef83150c68c2d`

These supersede the earlier `NX-Cast-candidate` artifacts, which used the
system libuam. The image's `toolchain-info` includes separate mpv/libuam source
records: `packages.txt` alone does not describe libraries installed from source.

These outputs include the current working-tree application code. They are not
a production release or a verified latest-Linux-toolchain build. The candidate
workflow has only been syntax-checked locally, not executed on GitHub.

Local source-build logs live under `artifacts/toolchain/candidate-research/`.
They are ignored by Git. A successful Mac cross-build does not validate the
Linux container, the latest Docker image, or hardware playback.

Before promotion:

The user reported the local source candidate worked on hardware. This does not
replace validation of the independently built Linux artifact below.

1. Complete the candidate Linux image and NX-Cast build in Actions.
2. Test DLNA playback, pause, seek, video switching, and exit.
3. Test IPTV startup, channel switching, and sustained playback.
4. Test AirPlay URL video and screen mirroring, including sound and rotation.
5. Test return to home and application exit, with and without nxlink logging.

Only after those pass should Build/Release consume the candidate resolver.
Keep the existing production image and prior dependency assets for rollback.
