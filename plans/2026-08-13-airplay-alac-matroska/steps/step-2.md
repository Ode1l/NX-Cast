# Step 2: Reproduce Switch FFmpeg Muxer Support

> Status: COMPLETED
> Created: 2026-08-13

## Goal
Provide a pinned, auditable build path for the same Switch FFmpeg with the required Matroska muxer enabled, and reject incomplete user-facing SD packages.

## Prerequisites
- Step 1 completed with Matroska selected as the bridge contract.
- Existing wiliwili FFmpeg recipe commit and archive checksum are verified.
- Files to modify: toolchain script, Dockerfile, makefile, workflows, release packaging, and toolchain/install documentation.

## Deliverables
- Reproducible custom `switch-ffmpeg` package build with ALAC decoder and Matroska muxer.
- Docker and GitHub Actions install and verify the custom package before building NX-Cast.
- Release packaging proves that the visible `switch/NX-Cast/` hbmenu entry contains exactly one launchable `NX-Cast.nro`.
- After this step: CI-equivalent dependency checks reject a muxer-less package.

## Plan
- [x] `write` `scripts/build_switch_ffmpeg_airplay.sh` — download verified pinned wiliwili source, patch only the muxer flag, and build/install the package.
- [x] `edit` `Dockerfile` — build and install the custom package in a non-root build user stage.
- [x] `edit` `makefile` — add an optional strict symbol/configuration guard for the AirPlay muxer.
- [x] `edit` `.github/workflows/build.yml`, `.github/workflows/release.yml`, and `scripts/docker_build_release.sh` — enable the strict guard consistently.
- [x] `edit` `scripts/package_release.sh` — reject a user-facing SD layout without the directory-local NRO or with an extra NRO entry.
- [x] `edit` `docs/ffmpeg-mpv-toolchain.md` — document local installation and verification.
- [x] `bash` run the package build and inspect archive symbols — require ALAC decoder and Matroska muxer.

## Quality Checklist
- [x] Evidence-before-edit: Docker, workflow, recipe, and docs targets read; validation commands identified.
- [x] Existing pattern / reuse checked: reuse pinned wiliwili recipe rather than forking FFmpeg patches.
- [x] Contract understood: package name/version satisfies existing libmpv dependency and preserves FFmpeg ABI.
- [x] Risk reviewed: supply chain, CI duration, root builds, package replacement, and local reproducibility.
- [x] Mitigation recorded: commit pin, SHA-256 verification, non-root package build, symbol guard.

## Validation Checklist
- [x] Toolchain script passes `bash -n`; POSIX release scripts pass `sh -n`.
- [x] Staged package exports `ff_alac_decoder`, `ff_h264_parser`, and `ff_matroska_muxer`.

## Test Checklist
- [x] Docker/CI command path uses `NXCAST_REQUIRE_AIRPLAY_MUXER=1` through `release-build`.

## Implementation Notes
- Added a pinned non-root package builder using wiliwili commit `88e5876bea9502d06f46a8656e3530684d3aaf7d`; all downloaded source and patch inputs have fixed SHA-256 checksums.
- Preserved `--disable-muxers` and enabled only `matroska`, producing `switch-ffmpeg-7.1-2-any.pkg.tar.zst` without changing the FFmpeg 7.1 ABI consumed by the existing libmpv package.
- Docker installs the baseline wiliwili stack first, builds the custom FFmpeg package as `nxcast-builder`, installs it, and verifies static-library symbols before accepting the image.
- `release-build` now refuses the old muxer-less package and records `airplay-matroska-muxer=1` in its release attestation.
- Release packaging validates the staged tree and final zip each contain exactly `switch/NX-Cast/NX-Cast.nro`; `sources.txt` and existing asset/license checks remain intact.
- Local package build completed successfully on 2026-08-13. The current system package intentionally failed the new strict guard because installing the generated package requires the user's sudo password; Step 3 uses a rootless staged portlibs tree for the strict link validation.

## Files Changed
- `.github/workflows/build.yml`
- `.github/workflows/release.yml`
- `Dockerfile`
- `README.md`
- `docs/ffmpeg-mpv-toolchain.md`
- `docs/install.md`
- `makefile`
- `scripts/build_switch_ffmpeg_airplay.sh`
- `scripts/docker_build_release.sh`
- `scripts/package_release.sh`
