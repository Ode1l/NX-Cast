# Step 1: Coordinator Contract And Host Tests

> Status: COMPLETED
> Created: 2026-07-21

## Goal
Add a thread-safe coordinator state machine and media transaction contract with focused host coverage independent of Switch hardware.

## Prerequisites
- User-confirmed architecture: concurrent listeners, single player owner, application-level coordinator.
- Files to modify: `source/app/protocol_coordinator.c`, `source/app/protocol_coordinator.h`, `scripts/test_protocol_coordinator.c`, `Makefile`.
- Existing primitive: `source/player/core/ownership.c` generation leases and transition mutex.

## Deliverables
- Coordinator lifecycle, service status, active-owner snapshot, and serialized media transaction APIs.
- Host tests for startup/degraded state, takeover, stale lease rejection, and idempotent stop.
- After this step: `make test-protocol-coordinator` passes without Switch hardware.

## Plan
- [x] `read` `source/player/core/ownership.*` and `source/protocol/airplay/airplay.*` — align mutex/state naming and lifecycle conventions.
- [x] `write` `source/app/protocol_coordinator.h` and `source/app/protocol_coordinator.c` — implement the state/event contract and ownership-backed transactions.
- [x] `write` `scripts/test_protocol_coordinator.c` — cover lifecycle reduction and concurrent takeover behavior.
- [x] `edit` `Makefile` — add `source/app`, a focused host-test binary, and `test-protocol-coordinator` target.
- [x] `bash` `make test-protocol-coordinator && git diff --check` — expect all checks to pass.

## Quality Checklist
- [x] Evidence-before-edit: target read `source/player/core/ownership.*`, impact search `rg player_ownership_ source`, validation `make test-protocol-coordinator`
- [x] Existing pattern / reuse checked: ownership leases and AirPlay lifecycle enum/state were inspected rather than duplicated blindly.
- [x] Contract understood: lifecycle mutations are thread-safe; service side effects occur outside state locks; media transaction holds the transition lock until end.
- [x] Risk reviewed: concurrency, stale callbacks, lock ordering, future protocol extensibility.
- [x] Mitigation recorded: generation validation, idempotent APIs, host concurrency test, no renderer calls while holding state mutex.

## Validation Checklist
- [x] `make test-protocol-coordinator` exits 0
- [x] `git diff --check` exits 0

## Test Checklist
- [x] `make test-protocol-coordinator` — lifecycle and media arbitration tests pass.

## Implementation Notes
Implemented a portable coordinator mutex, lifecycle/service reducer, revisioned snapshots, serialized claim/guard/release transactions, and an explicit transaction abort path. A focused pthread test covers degraded startup, idempotent stopping, owner takeover, stale lease rejection, transaction rollback, guards, and concurrent claims. Worktree protection via stash/commit was intentionally skipped because it would capture unrelated in-progress AirPlay changes; focused diffs were reviewed instead.

## Files Changed
- `source/app/protocol_coordinator.h`
- `source/app/protocol_coordinator.c`
- `scripts/test_protocol_coordinator.c`
- `Makefile`
