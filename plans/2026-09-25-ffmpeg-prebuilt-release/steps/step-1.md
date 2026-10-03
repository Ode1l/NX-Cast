# Step 1: Verified prebuilt dependency path

> Status: COMPLETED
> Created: 2026-09-25

## Goal
Make normal local installation and Docker CI consume a pinned GitHub Release FFmpeg asset, while preserving the source-builder target.

## Prerequisites
- Local 7.1-3 package and its digest are known.
- Existing install, source-build, Docker, and workflow files were inspected.

## Deliverables
- Shared verified fetch script, local install path, Docker path, and documentation.
- After this step, local file-URL fetch tests and host tests pass.

## Plan
- [x] `write` `scripts/fetch_switch_ffmpeg_airplay.sh` — fetch fixed release asset to a temporary file, validate SHA-256, reuse valid cache.
- [x] `edit` `scripts/install_switch_ffmpeg_airplay.sh` — call fetch instead of source build before `dkp-pacman -U`.
- [x] `edit` `Dockerfile` and `.github/workflows/toolchain.yml` — install the verified prebuilt package and key changes into image cache.
- [x] `edit` `README.md`, `docs/ffmpeg-mpv-toolchain.md`, and `docs/ci-toolchain.md` — document download/install and separate source fallback.
- [x] `bash` syntax, local fetch, tamper rejection, host suite, and diff check.

## Quality Checklist
- [x] Evidence-before-edit: targets read, `rg` across references, validation commands identified.
- [x] Existing pattern / reuse checked: existing build and install scripts; Wiliwili release-download pattern.
- [x] Contract understood: package name/digest fixed; no unchecked binary install.
- [x] Risk reviewed: unavailable asset, corrupted cache, dirty worktree, first CI run.
- [x] Mitigation recorded: atomic verified download, explicit error, source builder retained, selective staging.

## Validation Checklist
- [x] `bash -n` passes for changed shell scripts.
- [x] `git diff --check` passes.

## Test Checklist
- [x] Valid local file URL fetch and cache reuse pass.
- [x] Wrong cached file and wrong downloaded file are rejected.
- [x] `make test-airplay` passes.

## Implementation Notes
The new fetch script downloaded the local package through `file://`, reused a valid cache, rejected a corrupted cache plus invalid download, and recovered by downloading a valid copy. Extracted archive passed the FFmpeg verifier. Docker cannot be executed locally and the public URL awaits Step 2 publication.

## Files Changed
`scripts/fetch_switch_ffmpeg_airplay.sh`, `scripts/install_switch_ffmpeg_airplay.sh`, `Dockerfile`, `.github/workflows/toolchain.yml`, `README.md`, `docs/ffmpeg-mpv-toolchain.md`, `docs/ci-toolchain.md`.
