# Step 2: Publish dependency and app

> Status: IN_PROGRESS
> Created: 2026-10-04

## Goal
Make 0.3.2 and its exact FFmpeg dependency available through GitHub with a successful automatic build.

## Prerequisites
- Step 1 passed with revision-4 release artifacts.

## Deliverables
- Pushed release commit, checksum-verified FFmpeg dependency, v0.3.2 tag, complete stable release ZIP and successful Actions.

## Plan
- [ ] Commit scoped accumulated release changes without binaries/logs/secrets.
- [ ] Publish toolchain-ffmpeg-7.1-4 package against that commit and test the public downloader from a fresh directory.
- [ ] Push main and v0.3.2; follow GitHub toolchain/build/release results and fix actual build failures if any.
- [ ] Verify latest stable/Continuous labels, ZIP assets and deployed version.

## Quality Checklist
- [ ] Evidence: step 1 outputs, actual GitHub release/run APIs and upload hashes.
- [ ] Reuse: existing CI and release workflows; no workaround build pipeline.
- [ ] Contract: no forced main push, no old-tag movement, no previous asset replacement.
- [ ] Risk: CI downloading nonexistent/wrong dependency or helper release becoming latest.
- [ ] Mitigation: publish/verify dependency first; latest=false for toolchain and Continuous.

## Validation Checklist
- [ ] Public package digest matches pinned SHA; fresh fetch succeeds.
- [ ] GitHub Actions succeeds and v0.3.2 is latest stable.

## Test Checklist
- [ ] Downloaded installation ZIP version/layout/presets/sensitive-file checks pass.

## Implementation Notes
Staged snapshot review includes the accumulated user-tested source, matching FFmpeg recipe/patches, docs and development records. Logs, artifacts, distribution ZIPs and generated AirPlay identities are excluded from Git. Staged diff whitespace check passes excluding scripts/ffmpeg_nvtegra_long_ref.patch: its single-space blank line is a required unified-diff context marker, not program-source whitespace. git apply --numstat validates the patch format; its pinned checksum and tested binary must not be changed merely to strip that marker.

## Files Changed
Pending.
