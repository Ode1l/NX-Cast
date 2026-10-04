# Step 1: Integrate automatic FFmpeg preparation

> Status: COMPLETED
> Created: 2026-10-04

## Goal
Make current build commands install the pinned dependency only when needed and continue into the existing build safely.

## Prerequisites
- Makefile, install/fetch scripts, SDK rules, VS Code tasks and CI image recipe inspected.
- Clean main worktree; package version 7.1-4 already installed locally.

## Deliverables
- Idempotent installer, Make bootstrap, focused isolated validation and current bilingual documentation.

## Plan
- [x] Add package metadata lookup and installed-version/file detection to existing scripts.
- [x] Prepare dependency before build Make parsing, skip non-build commands, and track the pinned revision in build configuration.
- [x] Exercise current/missing/outdated package, failures and Make ordering without system mutations.
- [x] Run a real incremental build and update documentation/results.

## Quality Checklist
- [x] Evidence-before-edit: target scripts and callers read; validation defined above.
- [x] Reuse: existing GitHub fetch/cache/checksum, pacman and build-config stamp.
- [x] Contract: only pinned FFmpeg package installation is automated; sudo is restricted to installation.
- [x] Risk: parse-time failure, stale build products, repeated installation, root CI or custom-prefix confusion.
- [x] Mitigation: recipe-based outer dispatch, version stamp, fast path, explicit custom-prefix opt-out and failure propagation.

## Validation Checklist
- [x] Shell syntax and whitespace pass.
- [x] Current-version no-op and real build succeed.

## Test Checklist
- [x] Focused installer/Make tests pass without real package writes or network.

## Implementation Notes
The Makefile dispatches actual build entries through one dependency-preparation recipe before reading media package metadata. A recursive Make invocation preserves options/jobs and then uses the existing build rules. Clean/test/dry-run commands do not install packages. The installer queries the required filename from the existing fetch script, checks pacman's installed version and essential files, and returns immediately when ready. Missing/outdated files use the existing verified cache/download and package manager; installation failures propagate. Root containers need no sudo, and custom prefixes can opt out explicitly.

Ten isolated tests pass. The first test run exposed a cache-directory environment override being discarded by the Make entry; forwarding the existing environment fallback fixed it. The real local package check skipped installation. A complete dev-build passed without compiler warnings/errors; the next build was an incremental no-op with no download or installation. Logs: /tmp/nxcast-auto-ffmpeg-build.log and /tmp/nxcast-auto-ffmpeg-repeat.log. Syntax, whitespace and 22 documentation links passed. Tests are available locally and were not added as a new CI gate.

## Files Changed
- makefile
- scripts/fetch_switch_ffmpeg_airplay.sh
- scripts/install_switch_ffmpeg_airplay.sh
- scripts/test_ffmpeg_install.py
- README.md
- README_CN.md
- docs/ffmpeg-mpv-toolchain.md
- plans/2026-10-04-auto-ffmpeg/plan.md
- plans/2026-10-04-auto-ffmpeg/steps/step-1.md
