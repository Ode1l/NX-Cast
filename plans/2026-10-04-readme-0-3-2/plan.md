# Plan: Refresh bilingual README for 0.3.2

> Status: COMPLETED
> Created: 2026-10-04
> Last Updated: 2026-10-04

## Goal
Publish matching English/Chinese README pages describing the released 0.3.2 features, installation, controls and current developer dependencies.

## Assumptions
- The user requests the GitHub-facing README update as a follow-up to the authorized release publication.
- Documentation-only changes must not move the release tag or rebuild the accepted app binary.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] Both README pages introduce NX-Cast as a media center and describe DLNA, IPTV and experimental AirPlay accurately.
- [x] Installation uses one complete SD ZIP, preserves private pairing files/configuration, and distinguishes stable from Continuous.
- [x] Build instructions use the published pinned FFmpeg package and current make targets/workflows.
- [x] Local documentation links and whitespace checks pass; changes are pushed to main.
### Non-goals
- Playback changes, new compatibility claims, retagging 0.3.2, new CI checks or workflows.
### Edge Cases
- Audio-only AirPlay and DRM content are not supported; live streams/mirroring do not have normal seek timelines.

## Design Decisions
None - no design-sensitive changes. Preserve the logo and media-center description while reorganizing documentation for users before developers.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | steps/step-1.md | COMPLETED | Update bilingual README and linked install guidance, validate and publish |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Documentation links | Ruby local Markdown-link existence check | README link format | yes |
| Stale claims | rg version/dependency/unsupported terms in modified docs | released scripts and notes | yes |
| Whitespace | git diff --check | Git | yes |
| Publication | git status; git push origin main; gh api README readback | existing authenticated GitHub CLI | yes |
| Build/tests | N/A: documentation-only, accepted release build unchanged | release-0-3-2 validation | no |

## Context & Learnings
### Key Decisions
- Reuse docs/iptv.md and AirPlay development/compatibility docs for implementation details instead of duplicating protocol architecture in the landing README.
- Use [skip ci] for documentation-only publication; no extra app build or Continuous update is needed.
### Gotchas & Warnings
- Never replace real-device evidence with claims of Apple certification or complete AirPlay 2 support.
- Old docs/install.md incorrectly promises a standalone release NRO; current workflow uploads only the complete ZIP.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| README.md, README_CN.md | Public bilingual introduction | Read both; Chinese AirPlay/controls/build claims stale |
| docs/install.md | README installation destination | Read package layout and outdated NRO-only release claim |
| .github/release-notes.md | Accepted 0.3.2 scope | User-tested mirroring sound and experimental capability boundaries |
| Dockerfile, makefile, scripts/docker_build_release.sh | Build instructions | Revision-4 FFmpeg install and current release/dev/trace targets |
| .github/workflows/build.yml, release.yml | Publishing behavior | main-only Continuous, stable v* tags, ZIP-only assets |
| docs/iptv.md | Source configuration and navigation | SD paths, sources format, non-paged browsing and resource limits |
### Verified Facts
- APP_VERSION is 0.3.2; both stable and dependency releases already passed CI and fresh-download validation in the preceding release task.
- Docker installs NX-Cast's pinned switch-ffmpeg-7.1-4 directly, not Wiliwili's older baseline package.
- Chinese README omitted AirPlay, language controls, the channel drawer and libsodium, and still used v0.2.0 release instructions.
- Release workflows upload only dist/NX-Cast-sdmc.zip; main build retains host tests, while the tag release runs strict build/packaging without the host suite.
- Validated 24 local links/images, balanced Markdown fences, six Bash snippets with bash -n, selected obsolete claims and git diff --check; all passed.
- Commit 442df87 was pushed to main. GitHub API returned matching blob hashes for README.md, README_CN.md and docs/install.md; release artifacts/tags were not modified.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-04 | Step 1 | Refreshed bilingual user-first README for 0.3.2, corrected linked install instructions, verified commands/links and published with [skip ci]. |
