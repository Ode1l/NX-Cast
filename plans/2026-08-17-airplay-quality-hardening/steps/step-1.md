# Step 1: Explicit Remote Session State

> Status: COMPLETED
> Created: 2026-08-17

## Goal
Replace `active`/`hls_pending` booleans with an explicit remote-video session state and make `ACTION_READY` promote ownership before player I/O.

## Prerequisites
- Protected baseline commit `019bdae`.
- Files to modify: `source/protocol/airplay/media/remote_video.c`, `scripts/test_airplay_remote_video.c`.

## Deliverables
- `IDLE`, `PENDING_HLS`, and `ACTIVE` are the only remote session states.
- `owner_claimed` records whether a global lease exists.
- Reverse HLS action-ready sets `ACTIVE` and `owner_claimed` before `load()`/`play()`.
- Cleanup and stop paths release exactly when a claimed owner exists.

## Plan
- [x] `edit` source/protocol/airplay/media/remote_video.c — add private state enum, `state`, and `owner_claimed` fields.
- [x] `edit` source/protocol/airplay/media/remote_video.c — update helpers, `handle_play`, `handle_action`, `handle_rate`, `handle_scrub`, `handle_stop`, `session_closed`, and destroy cleanup.
- [x] `edit` scripts/test_airplay_remote_video.c — update existing pending/active assertions and add load-failure release coverage.
- [x] `bash` make test-airplay — expect exit 0.

## Quality Checklist
- [x] Evidence-before-edit: target files re-read; impact search `rg hls_pending|remote->active`; validation `make test-airplay`.
- [x] Existing pattern / reuse checked: existing generation guard and remote mutex remain the synchronization boundary.
- [x] Contract understood: direct playback remains supported without claim callback; reverse HLS requires it.
- [x] Risk reviewed: correctness / API / concurrency.
- [x] Mitigation recorded: state promotion before player I/O plus ownership tests.

## Validation Checklist
- [x] `git diff --check` exits 0

## Test Checklist
- [x] `make test-airplay` — all pass

## Implementation Notes
Added a private `AirPlayRemoteVideoSessionState` enum and `owner_claimed`. `ACTION_READY` now holds the remote mutex through claim, state promotion, load, and play, so stop/session-close cannot observe an owner-claimed pending state. Direct URL playback revalidates the generation after its optional claim and also holds the state lock through load/play; a stop during claim releases the just-claimed owner and returns `409`. Load-failure and direct claim-cancellation cleanup are covered by focused regression tests.

## Files Changed
- `source/protocol/airplay/media/remote_video.c`
- `scripts/test_airplay_remote_video.c`
