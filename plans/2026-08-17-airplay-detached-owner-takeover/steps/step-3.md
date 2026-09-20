# Step 3: Add Takeover Lifecycle Regression Coverage

> Status: COMPLETED
> Created: 2026-08-17

## Goal
Pin the corrected lifecycle with host tests proving detached AirPlay playback survives control close and that a later DLNA/IPTV claim force-replaces the retained owner.

## Prerequisites
- Steps 1-2 completed.
- Files to modify: `scripts/test_protocol_coordinator.c`, `scripts/test_airplay_remote_video.c`.

## Deliverables
- Coordinator tests cover AirPlay remote-video and mirror takeover, callback failure, same-resource replacement, and stale-lease races.
- Remote-video tests continue to prove final logical close does not call stop or release.
- Host AirPlay suite passes.

## Plan
- [ ] `read` scripts/test_protocol_coordinator.c — inspect existing exclusive and concurrent takeover tests.
- [ ] `read` scripts/test_airplay_remote_video.c — inspect existing active and pending close tests.
- [ ] `edit` scripts/test_protocol_coordinator.c — add takeover success/failure/race tests using the fake callback.
- [ ] `edit` scripts/test_airplay_remote_video.c — retain current close assertions and add a same-session re-entry case if not already covered.
- [ ] `bash` make test-airplay — expect exit 0.

## Quality Checklist
- [ ] Evidence-before-edit: tests and current expectations read; impact search callback name; validation `make test-airplay`.
- [ ] Existing pattern / reuse checked: existing fake runtime and ownership transition helpers.
- [ ] Contract understood: callback failure preserves old owner; callback stale lease cannot release new owner.
- [ ] Risk reviewed: concurrency / lifecycle / regression.
- [ ] Mitigation recorded: deterministic fake ownership release and exact assertion counts.

## Validation Checklist
- [ ] `git diff --check` exits 0

## Test Checklist
- [ ] `make test-airplay` — all pass

## Implementation Notes
- Added mirror-to-IPTV takeover to the coordinator contract test.
- Added a deterministic stale-release race test where a newer AirPlay owner appears inside the callback and the old lease release cannot corrupt it.
- Added a remote-video test proving the same logical session can reconnect and replace media after close without an intermediate stop/release.

## Files Changed
- `scripts/test_protocol_coordinator.c`
- `scripts/test_airplay_remote_video.c`
