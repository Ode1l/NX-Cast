# Step 1: Coordinator Suspension Lifecycle

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Expose a nonblocking discovery-suspension operation from the coordinator and prove correct first-claim, takeover, stale-release, final-release, abort, and reset behavior.

## Prerequisites
- User confirmed central single-media-owner behavior should drive discovery suspension.
- Files to modify: `source/app/protocol_coordinator.h`, `source/app/protocol_coordinator.c`, `scripts/test_protocol_coordinator.c`.
- Existing `make test-protocol-coordinator` host target is available.

## Deliverables
- Optional coordinator operation callback and observable snapshot suspension state.
- Focused host regression coverage for all ownership lifecycle edges.
- After this step: `make test-protocol-coordinator` passes.

## Plan
- [x] `read`/`rg` coordinator source and test callers — reconfirm locking, ownership, and FakeRuntime conventions before editing.
- [x] `edit` `source/app/protocol_coordinator.h` and `source/app/protocol_coordinator.c` — add idempotent callback state and invoke callbacks outside the mutex.
- [x] `edit` `scripts/test_protocol_coordinator.c` — add suspension event assertions across claim/takeover/release/abort/reset.
- [x] `bash` `make test-protocol-coordinator` — expect zero failures.

## Quality Checklist
- [x] Evidence-before-edit: target read, `rg` of ownership transitions/callers, validation `make test-protocol-coordinator`.
- [x] Existing pattern / reuse checked: extended `ProtocolCoordinatorOperations` and existing FakeRuntime rather than add a parallel coordinator.
- [x] Contract understood: callback is optional, nonblocking, idempotent, and runs outside the coordinator mutex.
- [x] Risk reviewed: concurrency, stale ownership tokens, shutdown, direct reset.
- [x] Mitigation recorded: callback transition tests, reentrant snapshot read, and snapshot assertions.

## Validation Checklist
- [x] Host test compiles with `-Wall -Wextra -Werror -pedantic` from the existing target.
- [x] `make PROTOCOL_COORDINATOR_TEST_BIN=/f/VSCODE~1/NX-Cast/build/tests/test_protocol_coordinator test-protocol-coordinator` exits 0.

## Test Checklist
- [x] First claim emits exactly one suspend.
- [x] Takeover and stale release emit no resume.
- [x] Final release and abort emit exactly one resume.
- [x] Tick reconciliation resumes after direct ownership reset.

## Implementation Notes
Added an optional nonblocking callback and snapshot flag. The transition helper updates state under the coordinator mutex, then invokes the callback after unlocking; the fake callback re-enters `protocol_coordinator_get_snapshot()` to protect that contract. Owner takeover remains suspended, stale releases are ignored, and abort now keeps the ownership transition guard until its coordinator state is reconciled. The existing test target needed an MSYS `gcc` package and a short-path output override because its unquoted absolute target path cannot handle the workspace space; no Makefile change was folded into this step.

## Files Changed
- `source/app/protocol_coordinator.h`
- `source/app/protocol_coordinator.c`
- `scripts/test_protocol_coordinator.c`
