# Step 2: Validate Protocol Recovery

> Status: COMPLETED
> Created: 2026-09-02

## Goal
Verify the corrected status contract across the full AirPlay suite and Switch target.

## Prerequisites
- Step 1 completed with focused protocol tests passing.

## Deliverables
- Full validation evidence and a precise real-device trace contract.
- After this step: a new Full Trace build is ready for Bilibili and YouTube testing.

## Plan
- [x] `bash` `make test-airplay` — validate all AirPlay protocol and lifecycle paths.
- [x] `bash` `make dev-build BUILD_JOBS=4` — build the Switch target.
- [x] `bash` `git diff --check` — verify patch hygiene.
- [x] `edit` plan files — record results and residual hardware risk.

## Quality Checklist
- [x] Evidence-before-edit: Step 1 diff re-read and shared response impact searched.
- [x] Existing pattern / reuse checked: repository Makefile targets used.
- [x] Contract understood: validation cannot prove sender behavior without hardware.
- [x] Risk reviewed: unrelated dirty worktree and cross-protocol regressions.
- [x] Mitigation recorded: no unrelated changes reverted; full suite plus target build.

## Validation Checklist
- [x] `make test-airplay` exits 0.
- [x] `make dev-build BUILD_JOBS=4` exits 0.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] Focused and aggregate AirPlay tests pass together.

## Implementation Notes
- The focused remote-video test passes with current-position, over-duration, loading, playing, paused, and terminal snapshots.
- `make test-airplay` exits 0, including remote HLS, remote video, mirror/audio, pairing, mDNS, composed receiver, and direct HLS coverage.
- `make dev-build BUILD_JOBS=4` exits 0 and produces `NX-Cast.nro`.
- `git diff --check` exits 0.
- Hardware validation must confirm that Bilibili continues `/playback-info` polling after first-frame presentation. Full Trace now prints the exact state and range fields needed to diagnose any remaining divergence.

## Files Changed
- `plans/2026-09-02-airplay-playback-info-recovery/plan.md`
- `plans/2026-09-02-airplay-playback-info-recovery/steps/step-2.md`
