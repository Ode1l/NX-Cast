# Step 2: Publish dependency and app

> Status: COMPLETED
> Created: 2026-10-04

## Goal
Make 0.3.2 and its exact FFmpeg dependency available through GitHub with a successful automatic build.

## Prerequisites
- Step 1 passed with revision-4 release artifacts.

## Deliverables
- Pushed release commit, checksum-verified FFmpeg dependency, v0.3.2 tag, complete stable release ZIP and successful Actions.

## Plan
- [x] Commit scoped accumulated release changes without binaries/logs/secrets.
- [x] Publish toolchain-ffmpeg-7.1-4 package against that commit and test the public downloader from a fresh directory.
- [x] Push main and v0.3.2; follow GitHub toolchain/build/release results and fix actual build failures if any.
- [x] Verify latest stable/Continuous labels, ZIP assets and deployed version.

## Quality Checklist
- [x] Evidence: step 1 outputs, actual GitHub release/run APIs and upload hashes.
- [x] Reuse: existing CI and release workflows; no workaround build pipeline.
- [x] Contract: no forced main push, no old-tag movement, no previous asset replacement.
- [x] Risk: CI downloading nonexistent/wrong dependency or helper release becoming latest.
- [x] Mitigation: publish/verify dependency first; latest=false for toolchain and Continuous.

## Validation Checklist
- [x] Public package digest matches pinned SHA; fresh fetch succeeds.
- [x] GitHub Actions succeeds and v0.3.2 is latest stable.

## Test Checklist
- [x] Downloaded installation ZIP version/layout/presets/sensitive-file checks pass.

## Implementation Notes
Staged snapshot review includes the accumulated user-tested source, matching FFmpeg recipe/patches, docs and development records. Logs, artifacts, distribution ZIPs and generated AirPlay identities are excluded from Git. Staged diff whitespace check passes excluding scripts/ffmpeg_nvtegra_long_ref.patch: its single-space blank line is a required unified-diff context marker, not program-source whitespace. git apply --numstat validates the patch format; its pinned checksum and tested binary must not be changed merely to strip that marker.

- Release commit: bafe7d4fb1578353627d1b1a7b6ac6857a3115ca. Both annotated tags reference it; final documentation is committed separately with [skip ci], without moving those tags.
- Dependency: https://github.com/Ode1l/NX-Cast/releases/tag/toolchain-ffmpeg-7.1-4 . Asset switch-ffmpeg-7.1-4-any.pkg.tar.zst (11,414,010 bytes), SHA-256 bc6068b8dfee02356aa571fcc126143bb718d9b900d61f4813190039c82a81d6. Actual public fetch into a fresh directory passed; CI log confirms the same package installed and capabilities verified. Dependency release is not latest.
- Main/Continuous run: https://github.com/Ode1l/NX-Cast/actions/runs/37152486070 , success. Continuous remains prerelease with DO NOT DOWNLOAD warning.
- Stable release run: https://github.com/Ode1l/NX-Cast/actions/runs/37152486312 , success. Reused the new cached toolchain image; ordinary app CI did not rebuild FFmpeg.
- Stable release: https://github.com/Ode1l/NX-Cast/releases/tag/v0.3.2 , latest, not prerelease. NX-Cast-sdmc.zip is 19,847,716 bytes; downloaded SHA-256 6d9f29431963f161f8710eaf9d40acf2d78ea2540dbd2057a5487a60f8606b3a matches GitHub asset digest.
- Extracted public ZIP: embedded NACP and Home strings identify 0.3.2; exactly one switch/NX-Cast/NX-Cast.nro; sources.txt SHA-256 ec1665102353b051567c0551cdf1a30f583974b1574ce9e686c006a201917a63 matches repository presets; no identities, captures or private keys. Build log confirms normal profile and all trace switches 0.
- Local default portlibs still has revision 3. Future local default-prefix rebuilds require make install-airplay-ffmpeg once. End users need only the SD ZIP because FFmpeg is statically linked.

## Files Changed
- Release commit includes accumulated source/toolchain/docs changes reviewed in Step 1.
- plans/2026-10-04-release-0-3-2/plan.md
- plans/2026-10-04-release-0-3-2/steps/step-2.md
