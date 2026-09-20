# Step 1: Validate Reported AirPlay Playback Fixes

> Status: COMPLETED
> Created: 2026-08-21

## Goal
Prove that the current code prevents the two failures in the reported trace and make only test-driven corrections if validation fails.

## Prerequisites
- Existing 2026-08-17 AirPlay changes remain present in the worktree.
- Files to inspect or modify only if needed: `source/protocol/airplay/media/remote_video.c`, `source/protocol/airplay/media/remote_hls.c`, `source/protocol/airplay/protocol/handlers.c`, `source/protocol/airplay/media/mirror_runtime.c`, and their focused tests.
- Design: no new protocol behavior beyond the already established pending-HLS and audio/mirror split contracts.

## Deliverables
- Focused regression evidence for pending reverse-HLS controls and audio-only RECORD routing.
- Any demonstrated defect fixed with a focused test.
- After this step: `make test-airplay` and `git diff --check` pass.

## Plan
- [x] `read` current focused tests and `rg` relevant Makefile targets — confirm exact coverage and validation commands.
- [x] `bash` `make test-airplay` — identify any failing AirPlay protocol boundary.
- [x] `edit` only the failing AirPlay source/test files if a reproducible failure exists — no edit needed because all focused regressions passed.
- [x] `bash` `make test-airplay` and `git diff --check` — verify zero failures.
- [x] `bash` `make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1` — verify Switch integration when the local toolchain is available.

## Quality Checklist
- [x] Evidence-before-edit: target reads completed, impact search covered direct tests, validation command is `make test-airplay`.
- [x] Existing pattern / reuse checked: current explicit remote session state and split callback paths are reused.
- [x] Contract understood: pending HLS must not touch the backend; audio-only must not start mirror video playback.
- [x] Risk reviewed: protocol compatibility, ownership races, and regression to DLNA/IPTV coexistence.
- [x] Mitigation recorded: focused host regressions plus strict Switch build.

## Validation Checklist
- [x] `git diff --check` exits 0.
- [x] `make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1` exits 0 and produces `NX-Cast.nro`.

## Test Checklist
- [x] `make test-airplay` — all AirPlay host tests pass.

## Implementation Notes
The trace predates the current fixes. Existing changes already defer player controls until reverse-HLS readiness and split audio-only RECORD from mirror player handoff. No additional source edit was justified. Full host and Switch validation passed.

## Files Changed
- `plans/2026-08-21-verify-airplay-playback-fix/plan.md`
- `plans/2026-08-21-verify-airplay-playback-fix/steps/step-1.md`
