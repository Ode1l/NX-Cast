# Step 1: Reorder Docker Dependencies

> Status: COMPLETED
> Created: 2026-09-25

## Goal
Install the pinned NX-Cast FFmpeg package before Wiliwili's libmpv without fetching obsolete FFmpeg.

## Prerequisites
- `Dockerfile` and package metadata inspected.
- Files to modify: `Dockerfile`, `docker-compose.yml`, `scripts/docker_build_release.sh`.

## Deliverables
- Dockerfile downloads libuam and libmpv from Wiliwili, but FFmpeg only from NX-Cast.
- Dockerfile verifies FFmpeg and libmpv headers after installation.
- Docker invocation no longer passes the removed FFmpeg build argument.

## Plan
- [x] Edit `Dockerfile` to remove the old FFmpeg argument and download.
- [x] Install pinned FFmpeg before libmpv while preserving other dependency checks.
- [x] Remove obsolete build argument from Compose and release-build wrapper.
- [x] Run diff and static ordering checks.

## Quality Checklist
- [x] Evidence-before-edit: Dockerfile read, impacted package flow searched, validation identified.
- [x] Existing pattern / reuse checked: retain existing fetch and verification scripts.
- [x] Contract understood: libmpv installation requires a Switch FFmpeg package.
- [x] Risk reviewed: package ordering and Docker layer cache.
- [x] Mitigation recorded: preserve removal of base FFmpeg and install pinned FFmpeg before libmpv.

## Validation Checklist
- [x] `git diff --check -- Dockerfile docker-compose.yml scripts/docker_build_release.sh` exits 0.
- [x] Static Dockerfile inspection confirms one FFmpeg download and correct order.

## Test Checklist
- [x] Image build: unavailable locally because no Docker/Podman/container CLI exists; report this limitation.

## Implementation Notes
The libmpv package metadata declares an unversioned `switch-ffmpeg` dependency. Docker now installs libuam, NX-Cast FFmpeg, and libmpv in that order. `sh -n scripts/docker_build_release.sh` passes. The image build was not run locally because no container runtime is installed.

## Files Changed
- `Dockerfile`
- `docker-compose.yml`
- `scripts/docker_build_release.sh`
