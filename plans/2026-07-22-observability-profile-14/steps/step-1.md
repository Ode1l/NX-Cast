# Step 1: Profile and Runtime Resource Foundation

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Create the behavior-equivalent Profile 14 and a tested, low-overhead registry for app thread lifecycle and resource snapshots.

## Prerequisites
- User confirmed Profile 14 is observation-only and inherits Profile 13 behavior.
- Files to modify: `makefile`, `.vscode/tasks.json`, `source/app/network_diagnostics.*`, new runtime diagnostics files, and focused tests.

## Deliverables
- Profile 14 build flags match Profile 13 behavior flags and enable only observability macros.
- Thread lifecycle counters, resource snapshots, and expanded AirPlay socket categories have a host-testable API.
- `make ... test-runtime-diagnostics` and existing network diagnostics tests pass.

## Plan
- [x] `read/rg` profile flags, network counter contracts, test-target conventions, and build source discovery.
- [x] `write` focused runtime diagnostics tests for create/join/failure generations and snapshot formatting.
- [x] `edit` runtime/network diagnostics and Makefile/task profile plumbing with Profile 14 gating.
- [x] `bash` focused host tests and JSON parse; expect zero failures.

## Quality Checklist
- [x] Evidence-before-edit: target reads and impact search recorded in the plan working set.
- [x] Existing pattern / reuse checked: extended `network_diagnostics` atomics and Makefile host-test targets.
- [x] Contract understood: instrumentation is not connected outside Profile 14 and cannot affect protocol decisions.
- [x] Risk reviewed: atomics, firmware API availability, log overhead, dirty worktree.
- [x] Mitigation recorded: compile-time gate, event-only logs, safe unknown values, focused tests.

## Validation Checklist
- [x] Profile 14 CFLAGS inspection shows Profile 13 behavior flags plus observation flag only.
- [x] `.vscode/tasks.json` parses successfully.

## Test Checklist
- [x] `make ... test-runtime-diagnostics` passes.
- [x] `make ... test-network-diagnostics` passes.

## Implementation Notes
Added an atomic app-thread lifecycle registry and bounded resource formatter. Host builds report unavailable process metrics as `unknown`; Switch builds collect process memory, newlib heap, and free thread slots. Profile 14 duplicates all Profile 13 behavior flags and adds only `NXCAST_RUNTIME_OBSERVABILITY=1`. The test was run red before implementation. WIP commit/stash was intentionally skipped because the worktree contains extensive user-owned changes; all edits used narrow patches. No pre-commit configuration was found.

## Files Changed
- `.vscode/tasks.json`
- `makefile`
- `source/app/network_diagnostics.c`
- `source/app/network_diagnostics.h`
- `source/app/runtime_diagnostics.c`
- `source/app/runtime_diagnostics.h`
- `scripts/test_runtime_diagnostics.c`
- `plans/2026-07-22-observability-profile-14/plan.md`
- `plans/2026-07-22-observability-profile-14/steps/step-1.md`
- `plans/2026-07-22-observability-profile-14/steps/step-2.md`
- `plans/2026-07-22-observability-profile-14/steps/step-3.md`
