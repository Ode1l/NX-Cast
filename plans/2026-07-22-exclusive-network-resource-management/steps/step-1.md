# Step 1: Exclusive ownership and coordinator resource modes

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Make the coordinator enforce first-protocol-owner-wins and asynchronously converge enabled services to Home, IPTV-exclusive, DLNA-exclusive, or AirPlay-exclusive mode.

## Prerequisites
- User-confirmed exclusive protocol policy is recorded in `plan.md`.
- Files to modify: `source/app/protocol_coordinator.h`, `source/app/protocol_coordinator.c`, `scripts/test_protocol_coordinator.c`.
- Existing ownership and service worker behavior has been read and impact-searched.

## Deliverables
- Public resource-mode enum and desired/applied/transition state in coordinator snapshots.
- Cross-family claims are rejected; same-family replacement remains supported.
- A supervised resource transition worker stops non-owner services and restores configured services without holding coordinator/ownership locks.
- After this step: `make test-protocol-coordinator` passes with contention, idempotence, failure, and shutdown-during-transition coverage.

## Plan
- [x] `edit source/app/protocol_coordinator.h` — define resource modes, transition snapshot fields, and nonblocking background-suspension operation contract.
- [x] `edit source/app/protocol_coordinator.c` — map owners to protocol families, reject cross-family preemption, compute desired service set, and run stop/start convergence on a supervised worker outside locks.
- [x] `edit scripts/test_protocol_coordinator.c` — preserve legacy last-writer behavior when isolation is disabled and add deterministic Home/DLNA/AirPlay/IPTV transition, concurrent-claim, retry, and shutdown tests when enabled.
- [x] `bash make test-protocol-coordinator` — zero host test failures.

## Quality Checklist
- [x] Evidence-before-edit: all three targets re-read; impact searches covered coordinator config/operations and all media lifecycle calls.
- [x] Existing pattern / reuse checked: reused coordinator workers, revisioned snapshots, service operations, and ownership leases.
- [x] Contract understood: claim calls stay synchronous; blocking service callbacks and joins run without coordinator or ownership locks.
- [x] Risk reviewed: concurrency, deadlock, stale lease, partial restart, shutdown race.
- [x] Mitigation recorded: first-family gate, desired/applied modes, retryable convergence, resource-worker cancellation/join, and host concurrency tests.

## Validation Checklist
- [x] `make PORTLIBS_PREFIX=/opt/devkitpro/portlibs/switch PROTOCOL_COORDINATOR_TEST_BIN=/tmp/nxcast-test-protocol-coordinator test-protocol-coordinator` exits 0 under devkitPro MSYS2.
- [x] `git diff --check -- source/app/protocol_coordinator.h source/app/protocol_coordinator.c scripts/test_protocol_coordinator.c` exits 0.

## Test Checklist
- [x] First simultaneous cross-family claimant wins and the loser is rejected.
- [x] Same-family replacement is accepted without restarting its receiver.
- [x] Home and each exclusive mode converge to the expected service set.
- [x] Failed restart is visible and retryable; stop cancels and joins a deliberately blocked restart.

## Implementation Notes
- Added an opt-in `exclusive_media_resources` policy so Profiles 1-12 and normal builds retain their previous ownership behavior until Profile 13 is enabled.
- Resource mode is derived from the winning ownership family. A dedicated coordinator worker stops unneeded services in reverse order and starts required services in forward order; callbacks and joins occur outside coordinator/ownership locks.
- Desired and applied modes are separate. Failed restart attempts remain visible, retry after one second, and never silently report Home as applied.
- Added happy-path, same-family, simultaneous cross-family, failed-restart, and shutdown-during-blocked-restart tests. The shutdown test resets its cancellation latch before restart so it proves cancellation instead of returning early.
- Standard strict host test passed. ASan/UBSan execution was attempted but the installed MSYS2 compiler cannot link `-lasan`/`-lubsan`; sanitizer validation remains an environment limitation for the integrated validation step.
- No protection commit/stash was created because the worktree already contains user/earlier diagnostic changes; doing so would capture unrelated in-progress work without authorization.
- No repository pre-commit or formatter configuration was found by the configured-file search.

## Files Changed
- `source/app/protocol_coordinator.h`
- `source/app/protocol_coordinator.c`
- `scripts/test_protocol_coordinator.c`
