# Step 2: Detach Active Remote Playback From Connection Close

> Status: COMPLETED
> Created: 2026-08-17

## Goal
Clear pending Reverse HLS state on final logical connection close while leaving active direct playback and its coordinator owner intact for explicit stop, replacement, or shutdown.

## Prerequisites
- Step 1 completed.
- Files to modify: `source/protocol/airplay/media/remote_video.c`, `scripts/test_airplay_remote_video.c`.

## Deliverables
- `airplay_remote_video_session_closed()` no longer stops or releases active direct playback.
- Pending Reverse HLS is cleared and reset on connection close.
- Tests prove connection close leaves active playback untouched and a later play can replace it.

## Plan
- [ ] `read` source/protocol/airplay/media/remote_video.c — inspect `handle_play`, cleanup helpers, and `session_closed`.
- [ ] `rg` `airplay_remote_video_session_closed` — confirm receiver is the only production caller.
- [ ] `edit` source/protocol/airplay/media/remote_video.c — clear pending/active remote state without calling stop/release.
- [ ] `edit` scripts/test_airplay_remote_video.c — update session-close expectations and add replacement coverage.
- [ ] `bash` make test-airplay — expect exit 0.

## Quality Checklist
- [ ] Evidence-before-edit: target and caller/test read; impact search `airplay_remote_video_session_closed`; validation `make test-airplay`.
- [ ] Existing pattern / reuse checked: coordinator lease remains the shutdown/replacement boundary.
- [ ] Contract understood: close detaches control, does not end owned playback.
- [ ] Risk reviewed: ownership / lifecycle / cleanup.
- [ ] Mitigation recorded: focused direct and pending regression tests.

## Validation Checklist
- [ ] `git diff --check` exits 0

## Test Checklist
- [ ] `make test-airplay` — all pass

## Implementation Notes
- `airplay_remote_video_session_closed()` now clears the bound remote-video state and resets pending HLS state without calling player `stop()` or `release_owner()`.
- A new `/play` can claim a replacement owner after the previous logical connection close; explicit `/stop` and integration shutdown still release retained owners.
- Updated the handler-level lifecycle test and added focused active-close and pending-HLS-close coverage.

## Files Changed
- `source/protocol/airplay/media/remote_video.c`
- `scripts/test_airplay_remote_video.c`
- `scripts/test_airplay_handlers.c`
