# Step 2: Promote And Observe Network Budgets

> Status: COMPLETED
> Created: 2026-08-13

## Goal
Use the proven production network settings and expose configured capacity plus instrumented operation pressure in runtime diagnostics.

## Prerequisites
- Step 1 completed with cache behavior protected.
- Files to modify: `Makefile`, `source/main.c`, `source/app/runtime_diagnostics.[ch]`, and `scripts/test_runtime_diagnostics.c`.

## Deliverables
- Normal/release builds use BSD12, `sb_efficiency=8`, and exclusive media resources.
- Diagnostic profiles retain their intended explicit variants.
- Resource snapshots report configured BSD capacity, efficiency, instrumented sockets/active operations, slot overflow, and oldest operation age.
- After this step: runtime/network/coordinator host tests pass.

## Plan
- [x] `edit Makefile` — apply production resource flags only to normal/release while retaining profile overrides.
- [x] `edit source/app/runtime_diagnostics.[ch]` — add configured-capacity and aggregate instrumented-operation fields with unambiguous labels.
- [x] `edit source/main.c` — configure diagnostics after socket initialization and include budget values in startup/heartbeat summaries.
- [x] `edit scripts/test_runtime_diagnostics.c` — verify capacity, aggregation, pressure formatting, and reset behavior.
- [x] `bash make test-runtime-diagnostics test-network-diagnostics test-protocol-coordinator` — all tests passed.

## Quality Checklist
- [x] Evidence-before-edit: targets read, profile flag impact searched, focused validation identified
- [x] Existing pattern / reuse checked: extend runtime/network diagnostics rather than add a new monitor
- [x] Contract understood: BSD sessions are concurrent service-call capacity, not socket limit
- [x] Risk reviewed: build-profile regression, misleading metrics, resource pressure
- [x] Mitigation recorded: normal-only promotion and instrumented labels

## Validation Checklist
- [x] Focused runtime/network/coordinator targets exit 0
- [x] `git diff --check` exits 0

## Test Checklist
- [x] Capacity unset/set, active operation aggregation, and formatter output pass

## Implementation Notes
- Normal/release builds now use BSD12, `sb_efficiency=8`, the existing 10-second DLNA controller timeout, and exclusive media resource coordination.
- Resource and network heartbeat logs label all socket/operation counters as instrumented values; libmpv/FFmpeg internal sockets remain outside these counters.
- `instrumented_pressure` is a heuristic that becomes true when instrumented active operations reach configured BSD sessions. It is not a socket descriptor limit.
- The host test deliberately starts 12 operations against eight detail slots, proving pressure and overflow are reported independently.

## Files Changed
- `makefile`
- `source/main.c`
- `source/app/runtime_diagnostics.c`
- `source/app/runtime_diagnostics.h`
- `scripts/test_runtime_diagnostics.c`
