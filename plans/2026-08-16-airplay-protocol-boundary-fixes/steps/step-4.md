# Step 4: Defer Reverse HLS Ownership

> Status: COMPLETED
> Created: 2026-08-16

## Goal
Make Reverse HLS negotiation claim `airplay-video` and submit player load only after `ACTION_READY`.

## Prerequisites
- Step 3 completed.
- Files to modify: `source/protocol/airplay/media/remote_video.c`, `scripts/test_airplay_remote_video.c`.

## Deliverables
- `/play` for a reverse HLS locator enters a pending negotiation state without claiming the player.
- `/action` request-next events remain pending; action-ready claims ownership, loads the local playlist, and plays.
- Controls and teardown handle pending negotiation without issuing null-URL player commands.
- Focused remote-video tests assert claim/load ordering.

## Plan
- [x] `edit` source/protocol/airplay/media/remote_video.c — add private HLS pending/active state and generation fields.
- [x] `edit` source/protocol/airplay/media/remote_video.c — update handle_play, handle_action, session_owned, rate, scrub, playback-info, stop, and cleanup paths for pending HLS.
- [x] `edit` scripts/test_airplay_remote_video.c — add fake reverse send/control-port ops and an HLS transcript test.
- [x] `bash` make test-airplay — expect exit 0.

## Quality Checklist
- [x] Evidence-before-edit: target remote-video control paths re-read; impact search `rg airplay_remote_video_route`; validation `make test-airplay`.
- [x] Existing pattern / reuse checked: existing generation validation and `airplay_remote_hls` state reused.
- [x] Contract understood: direct URL behavior unchanged; Reverse HLS ownership moves from `/play` to action-ready.
- [x] Risk reviewed: correctness / API / observability.
- [x] Mitigation recorded: pending generation guards and focused ownership-order tests.

## Validation Checklist
- [x] `git diff --check` exits 0

## Test Checklist
- [x] `make test-airplay` — all pass

## Implementation Notes
Added `hls_pending` and deferred player ownership for Reverse HLS. Pending control requests now store rate/scrub intent or return loading playback-info without calling libmpv. `ACTION_READY` claims ownership, loads the local playlist, and promotes the session to active.

## Files Changed
- `source/protocol/airplay/media/remote_video.c`
- `scripts/test_airplay_remote_video.c`
