# Step 1: Reusable Global Installation

> Status: COMPLETED
> Created: 2026-08-16

## Goal
Add reusable verification and installation entry points for the globally installed Switch FFmpeg package.

## Prerequisites
- `scripts/build_switch_ffmpeg_airplay.sh` produces `switch-ffmpeg-7.1-2-any.pkg.tar.zst`.
- `makefile` already detects the required symbols under `PORTLIBS_PREFIX`.
- The user has authorized global Matroska installation support.

## Deliverables
- `scripts/verify_switch_ffmpeg_airplay.sh` validates one installed prefix.
- `scripts/install_switch_ffmpeg_airplay.sh` builds and installs the package with the appropriate privilege boundary.
- Make targets expose build, install, and verification commands.
- After this step: local developers have one documented command surface for the global package.

## Plan
- [x] `write scripts/verify_switch_ffmpeg_airplay.sh` — centralize archive and symbol validation.
- [x] `write scripts/install_switch_ffmpeg_airplay.sh` — build as the current user and install with sudo or root-aware pacman invocation.
- [x] `edit makefile` — add `build-airplay-ffmpeg`, `install-airplay-ffmpeg`, and `verify-airplay-ffmpeg` targets.
- [x] `bash bash -n ...` — validate all toolchain scripts.

## Quality Checklist
- [x] Evidence-before-edit: target read `scripts/build_switch_ffmpeg_airplay.sh`, impact search `rg -n "matroska|release-build"`, validation `bash -n`.
- [x] Existing pattern / reuse checked: package build script exists and was extended rather than replaced.
- [x] Contract understood: package build is unprivileged; global install writes `/opt/devkitpro/portlibs/switch` through pacman.
- [x] Risk reviewed: privileged filesystem modification and accidental host/target archive confusion.
- [x] Mitigation recorded: explicit prefix checks, symbol checks, and no implicit install during ordinary builds.

## Validation Checklist
- [x] `bash -n scripts/build_switch_ffmpeg_airplay.sh scripts/install_switch_ffmpeg_airplay.sh scripts/verify_switch_ffmpeg_airplay.sh` exits 0.
- [x] `make -n build-airplay-ffmpeg install-airplay-ffmpeg verify-airplay-ffmpeg` resolves all targets.

## Test Checklist
- [x] `scripts/verify_switch_ffmpeg_airplay.sh /opt/devkitpro/portlibs/switch` reports the current missing muxer before installation or succeeds after installation.

## Implementation Notes
The package builder now invokes the shared verifier against the package's extracted target prefix. The installer deliberately refuses root execution during package construction, resolves the absolute pacman path, then uses sudo only for `dkp-pacman -U`. The current machine produced the expected focused missing-Matroska diagnostic.

## Files Changed
- `scripts/verify_switch_ffmpeg_airplay.sh`
- `scripts/install_switch_ffmpeg_airplay.sh`
- `scripts/build_switch_ffmpeg_airplay.sh`
- `makefile`
