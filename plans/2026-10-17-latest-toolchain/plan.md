# Plan: Rebuild media dependencies with the latest base toolchain

> Status: ACTIVE
> Created: 2026-10-17
> Last Updated: 2026-10-20

## Goal
Rebuild custom FFmpeg/mpv when the official latest base image changes, caching the complete resulting toolchain for application builds.

## Assumptions
- Equivalence between the existing Wiliwili deko3d binary and another mpv fork is unverified.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- Base image changes invalidate the complete media toolchain cache.
- Application-only changes reuse the existing toolchain.
- Custom media capabilities and patches remain intact.
- No incomplete source build replaces the working release image.
### Non-goals
- Application playback changes and Miracast work.
### Edge Cases
- Missing upstream source patches must block migration rather than silently substitute a different media implementation.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Toolchain updates | Fixed dated base vs latest | Latest base, source rebuild media dependencies, cache complete image | Yes, user request |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Establish complete media recipes and implement latest-base image reuse |
| Step 2 | `steps/step-2.md` | IN_PROGRESS | Integrate the verified source image into Build/Release without publishing a release |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Shell syntax | `bash -n scripts/build_switch_ffmpeg_airplay.sh` | Existing script | Yes after changes |
| Toolchain build | `docker build --pull .` | Dockerfile | Yes; Docker/container runtime unavailable in current shell |
| Application build | `make release-build` inside resulting image | Release workflow | Yes |

## Context & Learnings
### Key Decisions
- Preserve the working release configuration until the source dependency chain is complete.
### Gotchas & Warnings
- Rebuilding another fork is not evidence of equivalence to the current binary.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| Dockerfile | Base and media dependencies | Read |
| .github/workflows/toolchain.yml | Content-keyed image reuse | Read |
| scripts/build_switch_ffmpeg_airplay.sh | Existing verified FFmpeg recipe | Read |
| scripts/docker_build_release.sh | Local container entry point | Read |
| docker-compose.yml | Local container configuration | Read |
### Verified Facts
- Current base is devkitpro/devkita64:20260219; current cache key does not include upstream image identity.
- FFmpeg source script retains Matroska, libnx random, and nvtegra long-reference patches.
- Wiliwili commit 88e5876bea9502d06f46a8656e3530684d3aaf7d has scripts/switch/mpv_deko3d/PKGBUILD referencing mpv.patch, but the patch is absent from that tree. GitHub path history query returned no commits.
- NXMP BUILD.md references averne/mpv; its envideo branch contains render_dk3d.h. Equivalence to the Wiliwili binary has not been established.
- 2026-10-18: envideo was rejected after AV_PIX_FMT_ENVIDEO compilation failure. averne/mpv master commit 67c2e3f28be29a380a7eccdfdf59694bcd3ad417 uses nvtegra and compiled successfully.
- 2026-10-18: rebuilt FFmpeg 7.1-4 passed the existing feature verifier. Candidate mpv rebuilt against that FFmpeg; the application link map confirms all four main media archives came from the isolated sysroot.
- 2026-10-18: local devkitA64 r30-1 (GCC 16.1.0), libnx 4.12.0-1, deko3d 0.5.0-1. No Docker, Apple container, or Podman executable is available in the current shell. Linux image verification remains outstanding.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-17 | 1 | Read-only investigation found missing deko3d mpv patch. Production configuration remains unchanged. |
| 2026-10-18 | 1 | Added source candidate script, Horizon cross patch, isolated candidate Dockerfile/workflow and documentation. Local candidate NRO linked successfully. Candidate Linux workflow has not run; no commit or push. |
| 2026-10-20 | 1 | Rebuilt libuam with current GCC, linked full source-media candidate NRO, retained license/provenance, updated artifact hashes. Linux and hardware verification still outstanding. |
| 2026-10-20 | 1 | User passed local hardware test; candidate pushed. Fixed Linux fakeroot dependency; cloud run 39353197399 succeeded in 10m01s and cached rerun in 1m04s. Await Linux-artifact hardware test before production migration. |
