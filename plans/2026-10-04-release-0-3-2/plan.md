# Plan: Release NX-Cast 0.3.2

> Status: ACTIVE
> Created: 2026-10-04
> Last Updated: 2026-10-04

## Goal
Publish the user-tested mirror audio/video fixes as NX-Cast 0.3.2 with matching UI metadata, a complete SD ZIP, and the pinned revision-4 FFmpeg dependency available to local developers and CI.

## Assumptions
- User confirms the latest tested build has working sound and authorizes publishing accumulated project changes.
- CI may require an initial GHCR image build; no local Docker installation is required.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] Makefile, NACP, Home and release notes identify 0.3.2; formal build uses normal log policy and no trace switches.
- [ ] FFmpeg 7.1-4 package on GitHub matches the tested local package and pinned download SHA-256; local downloader and CI consume it.
- [x] Complete ZIP includes sources.txt/fonts/DLNA assets/licenses and excludes generated identities/captures.
- [ ] Version tag and release are published, CI succeeds, and 0.3.2 is the latest stable release; Continuous retains its warning.
### Non-goals
- New playback fixes or cache tuning, redesigning CI, modifying old release assets, claiming complete AirPlay 2 support.
### Edge Cases
- Never tag before publishing the dependency; do not publish the previous Full Trace NRO or global revision-3 FFmpeg build.

## Design Decisions
None - user corrected the requested version to 0.3.2 and specified the existing prebuilt GitHub Release dependency distribution.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | steps/step-1.md | COMPLETED | Version/docs updates, host checks, release build and package validation |
| Step 2 | steps/step-2.md | IN_PROGRESS | Commit/push, publish FFmpeg, tag and verify GitHub build/release |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| AirPlay regressions | make test-airplay | makefile | yes |
| UI regressions | sh scripts/test_ui.sh | build.yml | yes |
| FFmpeg capabilities | scripts/verify_switch_ffmpeg_airplay.sh <revision-4 prefix> | existing verifier | yes |
| Device release | make release-build RELEASE_JOBS=4 PORTLIBS="<revision-4 prefix> /opt/devkitpro/portlibs/switch" | makefile; prior build | yes |
| Install ZIP | scripts/package_release.sh | release.yml | yes |
| Dependency download | scripts/fetch_switch_ffmpeg_airplay.sh <fresh temporary directory> | Dockerfile/install target | yes |
| Remote validation | gh run/release/API readbacks and remote ZIP inspection | installed gh | yes |
| Whitespace | git diff --check | Git | yes |

## Context & Learnings
### Key Decisions
- Reuse checksum-pinned prebuilt releases and content-hashed GHCR image; never recompile FFmpeg in ordinary app CI.
- Force normal diagnostics in release-build so inherited trace arguments cannot leak into formal builds.
### Gotchas & Warnings
- Global portlibs remains revision 3; build locally with the staged revision-4 prefix first.
- Many user-tested changes are uncommitted; preserve and review rather than reset the worktree.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| makefile | App version and release recipe | APP_VERSION=0.3.1; normal default flags; release-build clean rebuild |
| source/protocol/dlna/server_info.c | Host fallback version | fallback 0.3.1 |
| source/player/render/imgui/imgui_overlay.cpp | Home version | NX-Cast + NXCAST_APP_VERSION |
| .github/release-notes.md, CHANGELOG.md, README.md | Release description | current notes/tag example 0.3.1 |
| scripts/fetch_switch_ffmpeg_airplay.sh | Pinned prebuilt download | revision 4 URL and bc6068... checksum |
| Dockerfile, .github/workflows/toolchain.yml | Cached CI toolchain | pinned package installation and input-hashed image |
| scripts/package_release.sh | Full SD ZIP | sources.txt integrity and generated-secret exclusion |
### Verified Facts
- Local package .PKGINFO says switch-ffmpeg 7.1-4; SHA-256 bc6068b8dfee02356aa571fcc126143bb718d9b900d61f4813190039c82a81d6 matches fetch script.
- No v0.3.3 or toolchain-ffmpeg-7.1-4 remote tag existed at initial inspection; current main is 598fe45. Recheck v0.3.2 before publication.
- GitHub currently only hosts toolchain-ffmpeg-7.1-3; revision 4 must be uploaded before tagged CI.
- Existing source-build recipe pins upstream archives/patches and includes the nvtegra long-reference patch, Matroska muxer and libnx random fix.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-04 | Step 1 | Host/UI tests passed; fixed shared mpv helper visibility discovered by normal build; clean revision-4 release build and complete ZIP passed. NACP/Home 0.3.2 and all trace switches 0 verified. |
