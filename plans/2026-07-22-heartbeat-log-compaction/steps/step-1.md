# Step 1: Compact Recurring Heartbeat Output

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Produce shorter runtime heartbeat tuples and lower-frequency network summaries without changing two-second liveness or stall detection.

## Prerequisites
- `source/main.c` heartbeat formatter and callers have been read.
- Impact search found documentation consumers but no parser/test consumer of the current strings.
- Design: preserve two-second liveness and evaluate network stalls every two seconds.

## Deliverables
- `source/main.c` emits compact documented tuples and gates network summaries to ten seconds.
- `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md` explains the tuple order and cadence.
- After this step: focused diagnostics tests and the Profile 13 Switch build pass.

## Plan
- [x] `edit` `source/main.c` — separate network summary emission from stall evaluation, add a ten-second summary deadline, and replace verbose runtime fields with compact grouped tuples.
- [x] `edit` `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md` — update expected field names, tuple order, and network heartbeat cadence.
- [x] `bash` `make ... test-network-diagnostics` — verify socket/operation diagnostic accounting.
- [x] `bash` Profile 13 `make ... -j4` — verify the Switch target compiles and links.

## Quality Checklist

- [x] Evidence-before-edit: read `source/main.c`; impact search `rg -n "runtime-heartbeat|network-heartbeat|network-stall"`; validation commands identified from `makefile`/`.vscode/tasks.json`.
- [x] Existing pattern / reuse checked: reuse `NetworkDiagnosticSnapshot`, existing slow-operation loop, and existing build/test targets.
- [x] Contract understood: diagnostic-only formatted output; no protocol or media side effects.
- [x] Risk reviewed: observability and performance.
- [x] Mitigation recorded: retain liveness/stall cadence, document tuple order, and run focused test plus Switch build.

## Validation Checklist
- [x] Profile 13 Switch build exits 0 and produces an NRO with SHA-256 `6C706FD63E52C41ABD48E8557765FE65C2DDAFBA0691E29C1FAC294AE1AADC3D`.
- [x] `rg` confirms docs and source use the same compact tuple contract.
- [x] Modified source/document files were re-read; targeted diff and whitespace scans found no artifacts introduced by this step.

## Test Checklist
- [x] `make ... test-network-diagnostics` — passed concurrent-operation, error, overflow, underflow, reset, and invalid-input coverage.

## Implementation Notes
- Runtime liveness remains at two seconds but now emits a versioned grouped tuple instead of repeating descriptive names for every value.
- Network snapshots are still collected and slow operations are still checked every two seconds. Only the normal `[network-heartbeat]` summary is gated to ten seconds, reducing its emission count by 80%.
- Event-specific warning/error logs remain unchanged, so queue drops, transition failures, socket accounting faults, and active stalls remain visible.
- The focused test required an explicit short-path `NETWORK_DIAGNOSTICS_TEST_BIN` override because GNU Make resolves `CURDIR` to the workspace path containing spaces under MSYS. This is a build-path issue, not a test failure.
- The repository was already broadly dirty. No stash, cleanup, staging, or commit was performed, and unrelated changes were preserved.

## Files Changed
- `source/main.c`
- `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md`
- `plans/2026-07-22-heartbeat-log-compaction/plan.md`
- `plans/2026-07-22-heartbeat-log-compaction/steps/step-1.md`
- `NX-Cast.nro` (generated build output; ignored by Git)
