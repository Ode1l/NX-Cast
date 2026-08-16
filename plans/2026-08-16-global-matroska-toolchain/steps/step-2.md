# Step 2: CI Global Toolchain Wiring

> Status: COMPLETED
> Created: 2026-08-16

## Goal
Make Docker and both GitHub Actions workflows use and verify the same globally installed custom FFmpeg package.

## Prerequisites
- Step 1 completed with `scripts/verify_switch_ffmpeg_airplay.sh` available.
- Both workflows continue to build through `Dockerfile`.

## Deliverables
- Docker uses the shared verifier after package installation.
- Build and release jobs run a named global-toolchain verification before tests/build.
- After this step: CI fails with a focused FFmpeg capability error instead of a later linker or release-build error.

## Plan
- [x] `edit Dockerfile` — replace duplicated `nm` checks with the shared verifier after global `dkp-pacman -U`.
- [x] `edit .github/workflows/build.yml` — add explicit container toolchain verification before project tests.
- [x] `edit .github/workflows/release.yml` — add the same verification before tagged release builds.
- [x] `edit scripts/docker_build_release.sh` — use the same verification in local Docker release builds.

## Quality Checklist
- [x] Evidence-before-edit: target read `Dockerfile` and workflows; impact search `rg -n "ff_matroska_muxer|release-build"`.
- [x] Existing pattern / reuse checked: both workflows already share Dockerfile behavior; preserved that architecture.
- [x] Contract understood: Docker builds as non-root, installs as root, then mounted source builds against global portlibs.
- [x] Risk reviewed: CI network policy and toolchain drift.
- [x] Mitigation recorded: only local `dkp-pacman -U`, pinned SHA-256 inputs, shared verifier in every path.

## Validation Checklist
- [x] `rg -n "verify_switch_ffmpeg_airplay" Dockerfile .github/workflows scripts/docker_build_release.sh` shows all expected integration points.
- [x] `actionlint .github/workflows/build.yml .github/workflows/release.yml` exits 0 if available (not installed locally; skipped as allowed).

## Test Checklist
- [x] Static check confirms no new `dkp-pacman -S` invocation exists in CI paths.

## Implementation Notes
Docker copies both build and verifier scripts, builds the package as `nxcast-builder`, globally installs it as root, and immediately verifies the installed portlibs prefix. Both Actions workflows and the local Docker release wrapper repeat that inexpensive verification before project tests and release compilation. `actionlint` and PyYAML were unavailable locally; shell syntax and targeted workflow text checks passed.

## Files Changed
- `Dockerfile`
- `.github/workflows/build.yml`
- `.github/workflows/release.yml`
- `scripts/docker_build_release.sh`
