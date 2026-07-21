# Plan: Unified Protocol State Machine

> Status: COMPLETED
> Created: 2026-07-21
> Last Updated: 2026-07-21

<!--
  Plan-level status (lifecycle):
    DRAFT     — awaiting approval after clarification
    ACTIVE    — execution in progress
    COMPLETED — all steps done, verified
    ARCHIVED  — optional long-term archival state
  This is distinct from step-level status (PENDING|IN_PROGRESS|COMPLETED|BLOCKED)
  in `steps/step-N.md`. The pre-edit gate checks step status, not plan status.
-->

## Goal
Introduce one application-level state machine that owns IPTV, DLNA, and AirPlay service lifecycle and serializes their access to the shared player through an extensible media-session contract.

## Assumptions
- Discovery/listener services may remain online concurrently, but only one media source may own the shared player at a time.
- Existing protocol parsing, transport, UI behavior, and player backends remain unchanged unless needed to route lifecycle or ownership through the coordinator.
- The dirty worktree contains active AirPlay regression work and must not be reset, stashed, or bundled into an intermediate commit.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] `main.c` starts, observes, and stops IPTV, DLNA, and AirPlay through one coordinator API.
- [x] Protocol callbacks use coordinator media transactions instead of directly defining cross-protocol ownership policy.
- [x] Coordinator status reports lifecycle, per-service readiness, active owner, and generation for logging/UI consumers.
- [x] Host tests cover normal startup, degraded startup, owner takeover, stale leases, and ordered shutdown.
- [x] Existing AirPlay tests and strict Switch trace build pass.

### Non-goals
- Rewriting DLNA, IPTV, or AirPlay packet/session implementations.
- Running protocol callbacks on the main render thread or replacing their existing worker threads.
- Changing playback priority rules beyond latest valid media request wins.

### Edge Cases
- Network unavailable: IPTV remains usable while DLNA and AirPlay are disabled.
- One network service fails: the coordinator remains ready if another source is available.
- Concurrent protocol requests: ownership changes are serialized and stale callbacks cannot mutate the new session.
- Shutdown during asynchronous AirPlay startup: stop is idempotent and follows the same ordered state transition.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Service concurrency | Stop inactive discovery services vs keep listeners online | Keep listeners online; serialize only shared player ownership | yes — matches the user's request for state-managed protocols and current casting behavior |
| State-machine location | `main.c`, player layer, protocol layer, application layer | `source/app/protocol_coordinator.*` so `main.c` composes it and player primitives remain protocol-neutral | yes — user explicitly requested main-process coordination |
| Arbitration rule | Fixed protocol priority vs latest valid request wins | Latest valid request wins, protected by generation leases | yes — preserves current behavior while preventing stale callbacks |
| Failure policy | Any service failure stops the app vs degraded operation | Degraded operation with per-service status | yes — IPTV must remain available without network/AirPlay |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add and host-test the coordinator lifecycle and media transaction contract. |
| Step 2 | `steps/step-2.md` | COMPLETED | Move main-process protocol startup, status polling, and shutdown into the coordinator. |
| Step 3 | `steps/step-3.md` | COMPLETED | Route DLNA, IPTV, and AirPlay player mutations through coordinator transactions and run full regression validation. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Formatting | `git diff --check` | Existing repository workflow | yes |
| Focused test | `make test-protocol-coordinator` | New host target added in Step 1 | yes |
| Regression test | `make test-airplay` | Existing `Makefile` target | yes |
| Concurrency test | host compiler with `-fsanitize=thread` for coordinator/ownership test where supported | Existing ownership test pattern | yes |
| Switch build | `make clean && make -j4 TRACE_MEDIA=1 TRACE_INPUT=1 TRACE_AIRPLAY=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1` | Existing strict trace build flags | yes |

## Context & Learnings
### Key Decisions
- State machine and media ownership are separate concerns: the coordinator owns policy, while `player/core/ownership` remains the locking/generation primitive.
- Protocol service start/stop operations must execute outside coordinator locks because startup can create worker threads and callbacks.
### Gotchas & Warnings
- AirPlay startup is asynchronous, so `tick` must reconcile requested and running states without blocking the render loop.
- Existing source changes are uncommitted; protection is focused diffs and validation rather than stash/commit operations that would capture unrelated work.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/main.c` | Current lifecycle composition and shutdown ordering | `sed -n '880,980p'` and `sed -n '1650,1760p'` show direct IPTV/DLNA/AirPlay calls |
| `source/player/core/ownership.c` | Existing mutex and generation lease primitive | targeted read of complete implementation on 2026-07-21 |
| `source/iptv/iptv.c` | IPTV player ownership caller | `rg player_ownership_ source` on 2026-07-21 |
| `source/protocol/dlna/control/action/avtransport.c` | DLNA player ownership caller | `rg player_ownership_ source` on 2026-07-21 |
| `source/protocol/airplay/integration.c` | AirPlay remote/mirror ownership caller | `rg player_ownership_ source` on 2026-07-21 |
| `Makefile` | Source discovery and host regression targets | `sed -n '35,90p'` and `sed -n '400,460p'` on 2026-07-21 |
| `source/app/protocol_coordinator.c` | Application lifecycle state and serialized player arbitration | re-read plus `make test-protocol-coordinator`, Step 1 |
| `scripts/test_protocol_coordinator.c` | Host lifecycle, failure, stale-lease, and concurrency coverage | re-read plus passing focused test, Step 1 |
| `source/protocol/dlna/control/handler.c` | DLNA renderer event boundary and callback lifecycle | strict build plus ownership/lifecycle `rg`, Step 3 |
| `source/player/core/session.c` | Shared player lifecycle ownership | strict build plus player init/deinit caller audit, Step 3 |

### Verified Facts
- No application-level protocol coordinator exists; only player ownership and AirPlay-local lifecycle state were found — verified by `rg` over `source`, 2026-07-21.
- `main.c` currently starts IPTV, DLNA, and AirPlay directly and stops them directly in AirPlay/DLNA/IPTV order — verified by targeted `sed`/`rg`, 2026-07-21.
- DLNA, IPTV, AirPlay remote video, and AirPlay mirror all call `player_ownership_*` directly — verified by `rg -n "player_ownership_" source`, 2026-07-21.
- The Makefile enumerates source directories rather than recursively discovering new directories, so `source/app` must be added explicitly — verified by Makefile lines 47-72 and 285-292, 2026-07-21.
- Host pthread tests are already part of `make test-airplay`, providing a reusable pattern for coordinator concurrency tests — verified by Makefile test target, 2026-07-21.
- Coordinator release and reset paths use the same transition lock as claims, preventing an old release from clearing a newer active-owner snapshot — verified by source re-read and concurrent host tests, Step 1.
- `main.c` now has one coordinator start/tick/snapshot/stop lifecycle; direct protocol calls remain only in operation adapters — verified by `rg` and strict Switch build, Step 2.
- AirPlay's existing `starting` flag is now exported to distinguish asynchronous startup from terminal failure without interpreting UI strings — verified by integration source read and runtime operation tests, Step 2.
- IPTV, DLNA, and AirPlay protocol modules contain no direct `player_ownership_*` calls; claims, guards, releases, and teardown barriers all pass through the coordinator — verified by scoped `rg`, Step 3.
- Player init/deinit is now called only by `main.c`; DLNA handler setup no longer resets ownership or deinitializes the shared player — verified by caller audit and clean strict build, Step 3.
- Final validation passed: `make test-airplay`, focused coordinator tests, ThreadSanitizer, `git diff --check`, and a clean strict Switch trace build; NRO SHA-256 is `9953d512e251c1b962b98aa1dac0f4ec0fb4ac686c3d40b9c5df0202552c5506` — verified 2026-07-21.
- The first three Verified Facts describe the pre-refactor baseline and are superseded by the Step 2/3 facts; they remain as append-only planning evidence — verified during global reflection, 2026-07-21.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-07-21 | Step 1 | Added thread-safe lifecycle/service state, generation-backed media transactions and guards, plus focused host concurrency coverage. `make test-protocol-coordinator` and `git diff --check` pass. |
| 2026-07-21 | Step 2 | Moved IPTV/DLNA/AirPlay startup, tick status, degraded state, UI snapshot, and ordered shutdown behind injected coordinator operations. Focused tests and strict trace Switch build pass. |
| 2026-07-21 | Step 3 | Migrated all protocol renderer mutations to coordinator transactions/guards, filtered cross-owner DLNA events, and made main the sole shared-player lifecycle owner. Full host, TSAN, formatting, and clean Switch build validation pass. |
| 2026-07-21 | Reflection | Re-read plan, steps, coordinator contract, protocol call sites, and lifecycle callers; added public lock/order contract comments and found no remaining introduced issue. |
