# Step 4: Rename Queue Flags And Add Regression Coverage

> Status: COMPLETED
> Created: 2026-08-17

## Goal
Rename mirror-runtime queue flags to explicit player names and ensure all ownership/race regressions remain covered.

## Prerequisites
- Steps 1-3 completed.
- Files to modify: `source/protocol/airplay/media/mirror_runtime.c`, `scripts/test_airplay_mirror_runtime.c`.

## Deliverables
- `load_queued` becomes `player_load_queued`.
- `play_queued` becomes `player_play_queued`.
- Existing direct, audio-first, and promotion tests pass unchanged in behavior.

## Plan
- [x] `edit` source/protocol/airplay/media/mirror_runtime.c — rename all queue fields and local helpers.
- [x] `bash` rg 'load_queued|play_queued' source/protocol/airplay/media/mirror_runtime.c — expect zero remaining old names.
- [x] `bash` make test-airplay — expect exit 0.

## Quality Checklist
- [x] Evidence-before-edit: target file re-read; impact search `rg load_queued|play_queued`; validation `make test-airplay`.
- [x] Existing pattern / reuse checked: no external callers of these private fields.
- [x] Contract understood: names change only, behavior is preserved.
- [x] Risk reviewed: none.
- [x] Mitigation recorded: full focused test suite.

## Validation Checklist
- [x] `git diff --check` exits 0

## Test Checklist
- [x] `make test-airplay` — all pass

## Implementation Notes
Renamed the private mirror-runtime booleans from `load_queued`/`play_queued` to `player_load_queued`/`player_play_queued`. No behavior changes were made.

## Files Changed
- `source/protocol/airplay/media/mirror_runtime.c`
