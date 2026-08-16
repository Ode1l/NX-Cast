# Step 4: Lease And Media Boundary Hardening

> Status: COMPLETED
> Created: 2026-08-14

## Goal
Prevent stale AirPlay runtime events from affecting newer playback and expose the exact bridge-to-player handoff boundary.

## Prerequisites
- Step 3 completed — protocol stages are observable and stable.
- Files to modify: `source/protocol/airplay/integration.c`, mirror runtime/bridge files, focused tests where host-testable.
- Existing coordinator lease API remains the sole player ownership authority.

## Deliverables
- Mirror status callbacks validate runtime generation before releasing/aborting leases.
- First configuration, keyframe, media bytes, bridge bind, and player load are distinguishable in traces.
- After this step: stale runtime callbacks are harmless and pipeline failure domain is explicit.

## Plan
- [x] `read` integration lease and mirror callback paths — identify stale-generation mutations.
- [x] `rg` generation/status callbacks and first-media logging — enumerate existing guards and diagnostics.
- [x] `edit` integration/runtime/bridge code — add minimal generation guard and one-shot boundary logs.
- [x] `edit` focused tests — cover stale generation where host test seams exist without adding production-only abstractions.
- [x] `bash` `make test-airplay` — verify ownership and media runtime regression.

## Quality Checklist
- [x] Evidence-before-edit: target read integration/runtime, impact search generation users, validation `make test-airplay`
- [x] Existing pattern / reuse checked: coordinator lease generation and existing observability macros reused
- [x] Contract understood: stale callback may log but cannot mutate active lease/player state
- [x] Risk reviewed: lease leak, stale release, logging on media hot path
- [x] Mitigation recorded: equality guard, one-shot/rate-limited logs, ownership tests

## Validation Checklist
- [x] `make test-airplay` exits 0
- [x] `git diff --check` exits 0

## Test Checklist
- [x] Stale generation cannot release or abort current AirPlay lease
- [x] Existing mirror runtime and stream bridge tests pass

## Implementation Notes
- Added runtime generation to the existing bind/open/play/stop callback contract so asynchronous work cannot silently borrow the current lease.
- Bound the AirPlay mirror runtime generation and coordinator lease atomically in integration; status updates and lease clearing require both runtime and lease generations to match.
- Preserved the existing coordinator and media actor as the two ownership authorities. No additional global state machine or generic generation framework was introduced.
- Added one-shot first-config, first-keyframe, bridge first-write/first-read, and player handoff diagnostics. Existing periodic counters remain rate limited.
- The host runtime test now verifies every asynchronous player operation carries the generation of the session that created it. Existing player ownership tests cover stale coordinator lease rejection.

## Files Changed
- `source/protocol/airplay/integration.c`
- `source/protocol/airplay/media/mirror_runtime.h`
- `source/protocol/airplay/media/mirror_runtime.c`
- `source/protocol/airplay/media/stream_bridge.c`
- `source/protocol/airplay/mirror/mirror_session.c`
- `scripts/test_airplay_mirror_runtime.c`
- `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md`
