# Plan: Release NX-Cast 0.3.2

> Status: COMPLETED
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
- [x] FFmpeg 7.1-4 package on GitHub matches the tested local package and pinned download SHA-256; local downloader and CI consume it.
- [x] Complete ZIP includes sources.txt/fonts/DLNA assets/licenses and excludes generated identities/captures.
- [x] Version tag and release are published, CI succeeds, and 0.3.2 is the latest stable release; Continuous retains its warning.
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
| Step 2 | steps/step-2.md | COMPLETED | Commit/push, publish FFmpeg, tag and verify GitHub build/release |

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
- Global portlibs remains revision 3; the release was built with the staged revision-4 prefix first. Run make install-airplay-ffmpeg before future default-prefix local rebuilds.
- Accumulated user-tested changes were reviewed and committed without resetting the worktree.
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
- Initial main was 598fe45. Release commit bafe7d4fb1578353627d1b1a7b6ac6857a3115ca and annotated v0.3.2/toolchain-ffmpeg-7.1-4 tags were pushed without force or old-tag changes.
- FFmpeg revision 4 was published and freshly downloaded/checksummed before pushing the app tag. GitHub toolchain logs confirm revision-4 download, installation and capability verification.
- Existing source-build recipe pins upstream archives/patches and includes the nvtegra long-reference patch, Matroska muxer and libnx random fix.
- Main Actions run 37152486070 and release run 37152486312 completed successfully. GitHub latest release is v0.3.2; Continuous remains a prerelease titled DO NOT DOWNLOAD - NX-Cast Continuous (Development Build).
- Downloaded public NX-Cast-sdmc.zip SHA-256 is 6d9f29431963f161f8710eaf9d40acf2d78ea2540dbd2057a5487a60f8606b3a, matching GitHub's digest. Embedded NACP/Home version is 0.3.2; presets match assets/iptv/sources.txt. Exactly one NRO is included, with no generated identities/captures/private keys.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-04 | Step 1 | Host/UI tests passed; fixed shared mpv helper visibility discovered by normal build; clean revision-4 release build and complete ZIP passed. NACP/Home 0.3.2 and all trace switches 0 verified. |
| 2026-10-04 | Step 2 | Published checksum-pinned FFmpeg 7.1-4, pushed main and v0.3.2, verified successful Actions and re-downloaded the public stable ZIP. Recorded final evidence without changing the release tag. |
