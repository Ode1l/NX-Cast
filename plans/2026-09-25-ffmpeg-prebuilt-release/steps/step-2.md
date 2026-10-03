# Step 2: Publish and verify the fixed dependency asset

> Status: COMPLETED
> Created: 2026-09-25

## Goal
Publish the pinned FFmpeg package as an NX-Cast GitHub Release asset and confirm its public URL works independently of the local build directory.

## Prerequisites
- Step 1 completed.
- Package digest and installation contract validated.
- Only task-related source changes are selected for publication.

## Deliverables
- A fixed `toolchain-ffmpeg-7.1-3` Release with the package attached.
- After this step, a fresh download passes the same SHA-256 and archive verifier.

## Plan
- [x] `bash` inspect `git diff` and staged files — exclude unrelated worktree changes.
- [x] `edit` `scripts/docker_build_release.sh` and `Dockerfile` — remove obsolete source-build-only arguments and tools discovered during review.
- [x] `bash` commit and push task-related source changes, then create the dependency tag/Release with the local package.
- [x] `bash` download release asset into a fresh temporary directory and compare SHA-256 and contents.
- [x] `bash` inspect Actions status and report any remaining local/device verification gap.

## Quality Checklist
- [x] Evidence-before-edit: final diff, remote/tag status, and auth checked.
- [x] Existing pattern / reuse checked: `gh release` and existing toolchain workflow.
- [x] Contract understood: public fixed-tag asset URL; no app release triggered.
- [x] Risk reviewed: publishing wrong binary/commit, tag collision, unrelated worktree staging.
- [x] Mitigation recorded: fixed checksum, selective staging, fresh-download verification.

## Validation Checklist
- [x] GitHub Release asset exists at fixed tag.
- [x] Fresh download has pinned SHA-256.

## Test Checklist
- [x] `scripts/fetch_switch_ffmpeg_airplay.sh <fresh-dir>` succeeds against public asset.
- [x] Extracted archive passes `scripts/verify_switch_ffmpeg_airplay.sh`.

## Implementation Notes
Commit `598fe45` contains only the dependency pipeline and its documentation. Tag and Release `toolchain-ffmpeg-7.1-3` point to that commit. Fresh download matched SHA-256 `fc47e883da3693847e7a845e9de3b235c3449bd9103a046bdb200faf61f48b70` and passed archive verification. GitHub build run `36118884004` succeeded: the toolchain job downloaded and verified the release asset, then the application build and continuous release update passed. Local Docker and Switch device playback were not run.

## Files Changed
`Dockerfile`, `scripts/docker_build_release.sh` (committed in `598fe45`); public GitHub dependency Release asset.
