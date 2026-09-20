# Step 1: Direct Media Replacement State Machine

> Status: COMPLETED
> Created: 2026-08-21

## Goal
Implement generation-safe same-session replacement, failed-play retry, and detached-session takeover in the remote-video state machine.

## Prerequisites
- `plan.md` has `Open Questions: None.` and confirms mode-driven replacement semantics.
- Files to modify: `source/protocol/airplay/media/remote_video.c`, `source/protocol/airplay/media/remote_video.h`, `scripts/test_airplay_remote_video.c`.
- Existing dirty changes in these files have been inspected and will be preserved.

## Deliverables
- Focused regression tests for URL A to URL B replacement, retry after load failure, detached takeover, and stale close.
- Remote-video state distinguishes active attached media from active detached media without app-specific branches.
- After this step: the focused remote-video host test passes.

## Plan
- [x] `edit` `scripts/test_airplay_remote_video.c` — add failing assertions for same-session new URL, failed-load retry, detached takeover, and stale close isolation.
- [x] `edit` `source/protocol/airplay/media/remote_video.c` — add attachment state and atomic replacement/takeover rules while preserving generation checks.
- [x] `edit` `source/protocol/airplay/media/remote_video.h` — reviewed; retained callback ABI because leaving it unset is sufficient and minimizes scope.
- [x] `bash` focused host compile and `build/tests/test_airplay_remote_video` — all lifecycle tests pass.

## Quality Checklist
- [x] Evidence-before-edit: target read `remote_video.c`, impact search `rg remote_detached|session_closed|claim_owner`, validation `make build/tests/test_airplay_remote_video`.
- [x] Existing pattern / reuse checked: coordinator lease generation and existing Recorder counters are reused.
- [x] Contract understood: `/play` returns 200 only after load/play acceptance; explicit `/stop` releases; control close alone does not stop active media.
- [x] Risk reviewed: concurrency and stale asynchronous cleanup.
- [x] Mitigation recorded: serialize state transitions under the existing mutex and test stale session closure.

## Validation Checklist
- [x] `git diff --check` exits 0.
- [x] Focused host test binary builds without warnings treated as errors.

## Test Checklist
- [x] Direct host compile plus `build/tests/test_airplay_remote_video` — all pass; the makefile exposes this binary only through `test-airplay`.

## Implementation Notes
The original focused make target did not exist, so the exact compile recipe from `test-airplay` was used. The initial test failed on duplicate same-session claim and missing detached release, then passed after adding `control_attached` and replacement rules. The public callback field was not removed because Step 2 can stop registering the policy without changing the API.

## Files Changed
- `source/protocol/airplay/media/remote_video.c`
- `scripts/test_airplay_remote_video.c`
- `plans/2026-08-21-airplay-media-replacement/plan.md`
- `plans/2026-08-21-airplay-media-replacement/steps/step-1.md`
