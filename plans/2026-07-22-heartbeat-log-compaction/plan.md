# Plan: Compact Diagnostic Heartbeats

> Status: COMPLETED
> Created: 2026-07-22
> Last Updated: 2026-07-22

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
Reduce recurring diagnostic heartbeat formatting and nxlink traffic while preserving the evidence needed to detect UI freezes, ownership/resource transitions, media queue stalls, and network stalls.

## Assumptions
- Diagnostic builds still need a two-second runtime liveness signal.
- A complete network subsystem snapshot every ten seconds is sufficient when slow operations continue to emit `[network-stall]` on the two-second diagnostic pass.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] `[runtime-heartbeat]` remains at approximately two-second intervals and is materially shorter.
- [x] `[network-heartbeat]` is emitted at approximately ten-second intervals.
- [x] `[network-stall]` is still evaluated on every runtime heartbeat.
- [x] The Profile 13 Switch build succeeds and the diagnostics guide matches the new format/frequency.

### Non-goals
- Changing player, DLNA, AirPlay, SSDP, mDNS, or ownership behavior.
- Removing event-specific media or protocol traces.

### Edge Cases
- Nonzero resource failures, queue drops, timeouts, socket accounting faults, and active network stalls must remain visible.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Runtime heartbeat cadence | Keep 2 s; increase to 5–10 s | Keep 2 s so a log freeze remains quickly visible | yes — preserves the user's diagnostic workflow |
| Network detail cadence | Keep every 2 s; emit every 10 s while checking stalls every 2 s | Emit summary every 10 s and retain immediate stall checks | yes — directly reduces recurring work without weakening stall detection |
| Field representation | Existing verbose names; compact grouped tuples | Compact grouped tuples with documented field order | yes — requested shorter heartbeats |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Compact heartbeat output, document the tuple contract, and validate Profile 13. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Source contract | `rg -n "runtime-heartbeat|network-heartbeat|network-stall" source/main.c docs/AIRPLAY_FREEZE_DIAGNOSTICS.md` | Targeted impact search | yes |
| Build | `make TOPDIR="$PWD" THIS_MAKEFILE="$PWD/makefile" NXCAST_DIAG_PROFILE=full-owner-exclusive-bsd12 NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 -j4` | Existing VS Code diagnostic task | yes |
| Regression tests | `make TOPDIR="$PWD" THIS_MAKEFILE="$PWD/makefile" test-network-diagnostics` | Existing focused network diagnostic target | yes |

## Context & Learnings
### Key Decisions
- Preserve event-specific logs and only compact recurring heartbeat summaries.
- Keep error/stall detection cadence separate from the lower network-summary cadence.

### Gotchas & Warnings
- `service_workers` represents coordinator transition workers, not all protocol threads; the compact format must not imply otherwise.
- Existing documentation names the heartbeat fields and must be updated with the tuple order.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/main.c` | Formats runtime/network heartbeat output and checks network stalls | `rg` and targeted read on 2026-07-22 |
| `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md` | Defines diagnostic cadence and field interpretation | `rg` on 2026-07-22 |
| `scripts/test_network_diagnostics.c` | Existing focused validation for socket/operation counters | impact search on 2026-07-22 |
| `.vscode/tasks.json` | Source of the Profile 13 build invocation | targeted read on 2026-07-22 |
| `plans/2026-07-22-heartbeat-log-compaction/` | Execution and validation record | persisted on 2026-07-22 |

### Verified Facts
- Runtime and network heartbeat summaries are currently both emitted from the same two-second block — verified by `rg`/read of `source/main.c`, 2026-07-22.
- `[network-stall]` evaluation is currently coupled to `main_log_network_diagnostics()` and can remain frequent while its summary is gated separately — verified by read of `source/main.c`, 2026-07-22.
- No script parses the heartbeat strings as a machine-readable contract — verified by `rg` across `scripts` and `source`, 2026-07-22.
- The network summary is gated at 10,000 ms while `main_log_network_diagnostics()` still runs from the two-second runtime block and evaluates stalls unconditionally — verified by source re-read and `rg`, 2026-07-22.
- The focused diagnostics test passed under MSYS, including concurrency, error, overflow, underflow, reset, and invalid-input cases — verified by `make ... test-network-diagnostics`, 2026-07-22.
- Profile 13 compiled and linked successfully; the NRO contains the `v=2` heartbeat formats and has SHA-256 `6C706FD63E52C41ABD48E8557765FE65C2DDAFBA0691E29C1FAC294AE1AADC3D` — verified by build, `strings`, and `Get-FileHash`, 2026-07-22.
- The shared worktree already contains extensive unrelated changes and CRLF-normalized diffs; only the heartbeat source, diagnostic guide, and this plan were intentionally edited for this step.
- The earlier statement that both summaries were emitted from the two-second block records the pre-edit baseline; after this step only runtime/stall evaluation remains two-second, while the normal network summary is ten-second.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-07-22 | Step 1 | Added compact `v=2` grouped heartbeats, reduced network summaries to ten seconds while preserving two-second stall scans, documented the tuple contract, passed focused tests and the Profile 13 build. |
