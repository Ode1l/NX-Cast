# Step 2: Close Claim And Scrub Races

> Status: COMPLETED
> Created: 2026-08-17

## Goal
Require a claim callback for Reverse HLS ready and make pending scrub decisions under a single state lock.

## Prerequisites
- Step 1 completed.
- Files to modify: `source/protocol/airplay/media/remote_video.c`, `scripts/test_airplay_remote_video.c`.

## Deliverables
- Reverse HLS action-ready returns 503 without loading when `claim_owner` is NULL.
- Pending scrub is stored only while the session is still pending, without a second unlocked state check.
- Focused tests cover missing claim callback and pending scrub.

## Plan
- [x] `edit` source/protocol/airplay/media/remote_video.c — fail closed for missing claim callback in `handle_action`.
- [x] `edit` source/protocol/airplay/media/remote_video.c — refactor POST scrub to read and mutate pending/active state under one lock.
- [x] `edit` scripts/test_airplay_remote_video.c — add no-claim and pending-scrub assertions.
- [x] `bash` make test-airplay — expect exit 0.

## Quality Checklist
- [x] Evidence-before-edit: target scrub/action paths re-read; impact search `rg session_hls_pending`; validation `make test-airplay`.
- [x] Existing pattern / reuse checked: remote mutex helper remains the single synchronization point.
- [x] Contract understood: no claim means no global ownership and no player mutation.
- [x] Risk reviewed: correctness / API / concurrency.
- [x] Mitigation recorded: regression tests for both paths.

## Validation Checklist
- [x] `git diff --check` exits 0

## Test Checklist
- [x] `make test-airplay` — all pass

## Implementation Notes
`handle_action` now returns 503 when the remote-video instance has no claim callback. POST scrub now locks the session once, decides pending versus active, and only then performs the seek or stores the pending position.

## Files Changed
- `source/protocol/airplay/media/remote_video.c`
- `scripts/test_airplay_remote_video.c`
