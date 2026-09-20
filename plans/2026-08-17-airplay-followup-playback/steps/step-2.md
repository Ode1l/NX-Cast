# Step 2: Add Bounded Detached Playback Cleanup

> Status: COMPLETED
> Created: 2026-08-17

## Goal
Schedule a 1500 ms cleanup when an active direct AirPlay logical connection closes, and cancel it when a new successful play claims the owner.

## Prerequisites
- Step 1 completed.
- Files to modify: `source/protocol/airplay/media/remote_video.h`, `source/protocol/airplay/media/remote_video.c`, `source/protocol/airplay/integration.h`, `source/protocol/airplay/integration.c`, `source/main.c`, `scripts/test_airplay_remote_video.c`.

## Deliverables
- Remote-video ops expose a `control_detached` callback for active close.
- Integration stores a monotonic cleanup deadline and clears it on claim/release/shutdown.
- Main-loop tick stops and releases the old lease after the deadline.
- Focused host test verifies callback emission without immediate stop/release.

## Plan
- [ ] `read` source/protocol/airplay/media/remote_video.h — inspect ops struct.
- [ ] `read` source/protocol/airplay/media/remote_video.c — inspect `session_closed`.
- [ ] `edit` remote-video API and implementation — add control-detached callback.
- [ ] `read` source/protocol/airplay/integration.c — inspect claim/release/stop helpers.
- [ ] `edit` integration API and implementation — add detach state, deadline, and `airplay_integration_tick`.
- [ ] `edit` source/main.c — invoke the integration tick after player-view frame processing.
- [ ] `edit` scripts/test_airplay_remote_video.c — record control-detached invocation.
- [ ] `bash` make test-airplay — expect exit 0.

## Quality Checklist
- [ ] Evidence-before-edit: all target and test files read; impact search callback/lease names; validation `make test-airplay`.
- [ ] Existing pattern / reuse checked: monotonic clock and coordinator ownership release.
- [ ] Contract understood: deadline is advisory cleanup; new claim cancels it; stale lease cannot release new owner.
- [ ] Risk reviewed: lifecycle / concurrency / stale ownership.
- [ ] Mitigation recorded: generation/session comparison and bounded deadline.

## Validation Checklist
- [ ] `git diff --check` exits 0

## Test Checklist
- [ ] `make test-airplay` — all pass

## Implementation Notes
- Added `control_detached` to remote-video ops and emit it only for active direct playback.
- Integration schedules release after 1500 ms and clears the deadline on successful claim, release, takeover, or shutdown.
- `airplay_integration_tick()` is called from the main render loop and validates the lease before cleanup.
- Host remote-video tests and strict full-trace Switch build pass.

## Files Changed
- `source/protocol/airplay/media/remote_video.h`
- `source/protocol/airplay/media/remote_video.c`
- `source/protocol/airplay/integration.h`
- `source/protocol/airplay/integration.c`
- `source/main.c`
- `scripts/test_airplay_remote_video.c`
