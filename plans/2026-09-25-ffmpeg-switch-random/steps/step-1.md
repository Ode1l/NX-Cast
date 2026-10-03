# Step 1: Pinned FFmpeg RNG patch and package validation

> Status: COMPLETED
> Created: 2026-09-25

## Goal
Ensure both local and CI builds use and verify a Switch-native FFmpeg random source.

## Prerequisites
- User approved implementation.
- Pinned FFmpeg source and build pipeline inspected.

## Deliverables
- Versioned patch applied in package `prepare()`.
- Package verifier checks `randomGet` reference.
- Docker/CI image includes and keys the patch.
- Validation results recorded.

## Plan
- [x] `write` `scripts/ffmpeg_switch_random.patch` — implement `av_random_bytes` with libnx on Switch.
- [x] `edit` build/install/verify scripts — apply, checksum, version, and validate patch.
- [x] `edit` `Dockerfile` and `.github/workflows/toolchain.yml` — include patch in CI image and cache key.
- [x] `edit` `makefile` — prevent release builds against the old random source.
- [x] `bash` syntax, package build, verifier, and host tests; target build awaits global installation.

## Quality Checklist
- [x] Evidence-before-edit: scripts, recipe, upstream source, CI image read; call sites searched; commands identified.
- [x] Existing pattern / reuse checked: pinned patch/checksum flow in build script and `randomGet` in libnx.
- [x] Contract understood: Switch-only RNG branch, unchanged non-Switch behavior, package-level fail-closed verification.
- [x] Risk reviewed: global package install may need sudo; deterministic CI cache must change.
- [x] Mitigation recorded: package release bump, symbol verification, build/test commands.

## Validation Checklist
- [x] Bash syntax and `git diff --check` pass.
- [x] Package build passes; target build cannot use the package until privileged installation.

## Test Checklist
- [x] `make test-airplay` passes.
- [x] Patched archive passes verifier; installed old archive fails.

## Implementation Notes
The pinned package now patches FFmpeg `av_random_bytes` to call libnx `randomGet` on Switch. The package built and passed archive verification. `make -n NXCAST_REQUIRE_AIRPLAY_MUXER=1 release-build` fails before cleanup/linking on the still-installed old archive, as required. Docker image build and target link were not run locally because Docker is unavailable and installing the package requires the user's sudo password.

## Files Changed
`scripts/ffmpeg_switch_random.patch`, `scripts/build_switch_ffmpeg_airplay.sh`, `scripts/install_switch_ffmpeg_airplay.sh`, `scripts/verify_switch_ffmpeg_airplay.sh`, `Dockerfile`, `.github/workflows/toolchain.yml`, `makefile`.
