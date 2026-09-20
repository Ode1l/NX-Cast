# Step 1: Correct Playback Info Semantics

> Status: COMPLETED
> Created: 2026-09-02

## Goal
Make AirPlay playback status internally consistent and observable across loading, playing, and paused states.

## Prerequisites
- Baseline/current traces and UxPlay playback-info implementation inspected.
- Files to modify: `source/protocol/airplay/media/remote_video.c`, `scripts/test_airplay_remote_video.c`.

## Deliverables
- Regression assertions for loaded ranges and state booleans.
- Protocol-correct playback-info payload and redacted field diagnostics.
- After this step: focused remote-video tests pass.

## Plan
- [x] `edit` `scripts/test_airplay_remote_video.c` — assert position-relative loaded ranges for active and paused snapshots.
- [x] `edit` `source/protocol/airplay/media/remote_video.c` — compute bounded remaining loaded range and emit field-level trace data.
- [x] `bash` focused remote-video test — expect zero failures.

## Quality Checklist
- [x] Evidence-before-edit: target and reference read; impact search covered `/playback-info` callers and tests; focused validation identified.
- [x] Existing pattern / reuse checked: retain current snapshot callback and XML writer; add no new subsystem.
- [x] Contract understood: GET returns plist state derived from the owned player snapshot.
- [x] Risk reviewed: sender compatibility, floating-point bounds, trace noise, YouTube HLS polling.
- [x] Mitigation recorded: generic arithmetic, no host/app detection, focused plus full AirPlay tests.

## Validation Checklist
- [x] Focused remote-video test exits 0.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] Loading, playing, and paused playback-info payloads are consistent.
- [x] Position greater than duration cannot produce a negative loaded duration.

## Implementation Notes
The focused test failed against the old zero/full loaded range, then passed after the range was changed to clamped-position/remaining-duration. Full Trace now records the exact state fields sent to the controller without URLs or sender identifiers.

## Files Changed
- `source/protocol/airplay/media/remote_video.c`
- `scripts/test_airplay_remote_video.c`
- `plans/2026-09-02-airplay-playback-info-recovery/plan.md`
- `plans/2026-09-02-airplay-playback-info-recovery/steps/step-1.md`
