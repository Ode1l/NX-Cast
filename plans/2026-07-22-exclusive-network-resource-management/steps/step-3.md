# Step 3: Global bounded network diagnostics

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Expose bounded, allocation-free diagnostics for owned socket lifecycles and blocking operations so hardware logs can distinguish BSD-session starvation from protocol, player, or remote HTTP failure.

## Prerequisites
- Step 2 completed with stable resource transition boundaries.
- Files to create: `source/app/network_diagnostics.[ch]`, `scripts/test_network_diagnostics.c`.
- Files to modify: key AirPlay/DLNA socket workers, `source/main.c`, and `makefile` host test targets.

## Deliverables
- Atomic per-subsystem counters for open sockets, active operations, operation count, maximum/last duration, last errno, last operation, and heartbeat age.
- mDNS, SSDP, DLNA HTTP, and AirPlay control paths record socket open/close and select/accept/recv/send boundaries without logging payloads.
- Runtime heartbeat emits one bounded summary and flags operations exceeding the diagnostic threshold.
- After this step: `make test-network-diagnostics test-airplay-mdns-suspend test-protocol-coordinator` passes.

## Plan
- [x] `write source/app/network_diagnostics.h source/app/network_diagnostics.c` — add fixed-size atomic subsystem registry, monotonic operation tokens, snapshot/reset API, and no payload/URL storage.
- [x] `write scripts/test_network_diagnostics.c` — test nested/concurrent operations, socket balance, duration maxima, errno capture, bounded active-slot overflow, and reset.
- [x] `edit source/protocol/airplay/discovery/mdns.c source/protocol/dlna/discovery/ssdp.c` — instrument UDP socket lifetime and select/recv/send operations.
- [x] `edit source/protocol/http/http_server.c source/protocol/airplay/server.c` — instrument listener/client sockets and accept/recv/send operations.
- [x] `edit source/main.c` — append bounded registry snapshots and long-operation warnings to the existing runtime heartbeat.
- [x] `edit makefile` — add `test-network-diagnostics` and include it in the normal host suite.
- [x] `bash make test-network-diagnostics test-airplay-mdns-suspend test-protocol-coordinator` — all three exit 0.

## Quality Checklist
- [x] Evidence-before-edit: existing mDNS diagnostics and runtime heartbeat read; impact search covers selected socket calls.
- [x] Existing pattern / reuse checked: extended the existing atomic heartbeat/counter style without a tracing dependency.
- [x] Contract understood: diagnostics never block, allocate in hot paths, or store sensitive URLs/payloads.
- [x] Risk reviewed: logging recursion through nxlink, counter imbalance, atomic portability, hot-loop overhead.
- [x] Mitigation recorded: snapshots only in heartbeat, fixed enums/atomics, no per-operation logging, bounded active slots, and host concurrency tests.

## Validation Checklist
- [x] `make test-network-diagnostics` exits 0.
- [x] `make test-airplay-mdns-suspend` exits 0 and verifies mDNS diagnostic socket/operation balance.
- [x] `make test-protocol-coordinator` exits 0.
- [x] Profile 13 Switch build links all instrumented modules.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] Concurrent begin/end operations leave active count zero and preserve totals/max duration.
- [x] Open/close accounting detects underflow, never wraps, and reset is deterministic when no resources are active.
- [x] Runtime heartbeat formatting is bounded to a 1024-byte stack buffer and contains no media URL or packet body.

## Implementation Notes
- The registry tracks five fixed subsystems and at most eight detailed active-operation slots per subsystem; aggregate active/total counters remain accurate when the detail slots overflow.
- Socket operations write only atomic counters. Formatting and warnings happen on the existing two-second main heartbeat, avoiding log recursion from socket workers.
- A 1500 ms active-operation threshold emits `[network-stall]` with subsystem, operation kind, token, duration, active count, socket count, and errno only.
- mDNS, SSDP, DLNA HTTP, and AirPlay control now account for temporary, listener, accepted-client, and worker sockets through centralized local wrappers.

## Files Changed
- `source/app/network_diagnostics.[ch]`
- `scripts/test_network_diagnostics.c`
- `scripts/test_airplay_mdns_suspend.c`
- `source/protocol/airplay/discovery/mdns.c`
- `source/protocol/dlna/discovery/ssdp.c`
- `source/protocol/http/http_server.c`
- `source/protocol/airplay/server.c`
- `source/main.c`
- `makefile`
