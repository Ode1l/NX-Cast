# Plan: Remove Redundant Docker FFmpeg Install

> Status: COMPLETED
> Created: 2026-09-25
> Last Updated: 2026-09-25

## Goal
Build the Docker toolchain with the pinned NX-Cast FFmpeg package without first downloading Wiliwili's obsolete FFmpeg package.

## Assumptions
- The published NX-Cast FFmpeg package remains available at the pinned release URL and SHA-256.

## Open Questions
None.

## Spec-Lite
N/A — covered by Goal, Deliverables, and Validation.

## Design Decisions
None — no design-sensitive changes.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Reorder Docker package installation, remove stale build args, and verify the dependency path. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Diff check | `git diff --check -- Dockerfile` | Git | Yes |
| Static order check | `rg -n 'SWITCH_FFMPEG_PKG|fetch_switch_ffmpeg_airplay|SWITCH_LIBMPV_PKG' Dockerfile` | Dockerfile | Yes |
| Image build | `docker build ...` | Dockerfile | Unavailable locally; Docker CLI absent |

## Context & Learnings
### Key Decisions
- Install the pinned NX-Cast FFmpeg package before libmpv so libmpv dependencies are satisfied without the old package.
### Gotchas & Warnings
- No local Docker CLI, so image execution cannot be verified here.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `Dockerfile` | Toolchain package order | Read via `sed -n '1,80p' Dockerfile` |
| `docker-compose.yml` | Docker build arguments | Read via `sed -n '1,45p' docker-compose.yml` |
| `scripts/docker_build_release.sh` | Docker build arguments | Read via `sed -n '1,65p' scripts/docker_build_release.sh` |
| `scripts/fetch_switch_ffmpeg_airplay.sh` | Fixed package source | Referenced by Dockerfile and existing CI |

### Verified Facts
- Dockerfile currently installs Wiliwili's `switch-ffmpeg-7.1-1` and later replaces it with NX-Cast `7.1-3` — verified by Dockerfile read, 2026-09-25.
- The local `7.1-3` package declares Switch library dependencies but no libmpv dependency — verified from `.PKGINFO`, 2026-09-25.
- Docker CLI is absent locally — verified by `command -v docker`, 2026-09-25.
- Compose and release-build wrapper pass the obsolete FFmpeg package build argument — verified by `rg` and file reads, 2026-09-25.
- Wiliwili's deko3d libmpv package depends on `switch-ffmpeg` without a version constraint — verified from downloaded `.PKGINFO`, 2026-09-25.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-25 | Step 1 | Removed redundant FFmpeg package download and stale build arguments; static checks passed, container build unavailable locally. |
