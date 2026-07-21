# Step 2: Player Owned-Value Contracts

> Status: COMPLETED
> Created: 2026-07-21

## Goal
Make media, event, snapshot, and view value replacement consistently failure-atomic and explicitly tested.

## Prerequisites
- Step 1 completed and shared host-test targets available.
- Files to modify: `source/player/types.c`, `source/player/types.h`, relevant render value code, and focused tests.

## Deliverables
- Null, empty, self-copy, replacement, and clear semantics are explicit.
- Existing values are not leaked or destroyed when duplication fails.
- After this step: repeated owned-value lifecycle tests pass under ASan/UBSan.

## Plan
- [x] `rg` player owned-value callers — verify initialization and replacement assumptions at every API boundary.
- [x] `edit` `source/player/types.c` and `source/player/types.h` — fix null replacement and document ownership contracts.
- [x] `edit` `source/player/render/view.c` only if the same proved defect exists — keep behavior aligned with player types.
- [x] `write` `scripts/test_player_types.c` — cover self-copy, null replacement, repeated copy/clear, and failure-safe visible state where injectable.
- [x] `edit` `makefile` and `bash` focused/sanitized tests — include the new lifecycle test.

## Quality Checklist
- [x] Evidence-before-edit: target/callers read, impact search recorded, validation command known.
- [x] Existing pattern / reuse checked: typed `clear/copy/set` APIs retained.
- [x] Contract understood: destination owns dynamic members after success and remains valid after failure.
- [x] Risk reviewed: double-free, leak, aliasing, uninitialized destination.
- [x] Mitigation recorded: zero-initialized test fixtures and repeated ASan lifecycle loops.

## Validation Checklist
- [x] `make test-player-types` exits 0.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] Sanitized player type test passes.

## Implementation Notes
- Caller audit confirmed destinations are zero-initialized or already managed by typed APIs.
- `player_media_copy(out, NULL)` and `player_event_copy(out, NULL)` now invoke their clear functions rather than discarding owned pointers with `memset`.
- `player_view_status_copy()` has no NULL-source clear branch and already performs failure-atomic replacement, so no render file change was required.
- The test repeats set/deep-copy/self-copy/NULL-clear/dispose paths 1000 times and passes under ASan/UBSan.

## Files Changed
- `source/player/types.c`
- `source/player/types.h`
- `scripts/test_player_types.c`
- `makefile`
