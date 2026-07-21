# Step 1: Diagnostic Runtime Boundaries

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Add opt-in compile-time controls that isolate protocol startup ordering and each mDNS execution boundary without changing the normal build.

## Prerequisites
- The current coordinator and mDNS startup paths have been read.
- Files to modify: `makefile`, `source/app/protocol_coordinator.c`, `source/app/protocol_coordinator.h`, `source/main.c`, `source/protocol/airplay/integration.c`, `source/protocol/airplay/discovery/mdns.c`, and focused host tests.
- Design: diagnostic profiles remain compile-time and profile zero preserves current behavior.

## Deliverables
- Explicit controls for serial startup, AirPlay discovery, mDNS socket-only/idle/full behavior, and mDNS priority.
- Runtime startup lines identify the active profile and selected boundaries.
- After this step: focused host profile/coordinator tests pass.

## Plan
- [x] `read` relevant coordinator, integration, mDNS, and test sections - confirmed contracts and insertion points.
- [x] `edit` `makefile` - mapped one validated diagnostic profile to compile-time controls and profile label.
- [x] `edit` `source/app/protocol_coordinator.{h,c}` and `source/main.c` - added opt-in serial service startup while retaining supervised workers.
- [x] `edit` `source/protocol/airplay/integration.c` and `source/protocol/airplay/discovery/mdns.c` - added discovery-off, socket-only, idle-thread, receive-only, full, and priority controls.
- [x] `edit` focused host tests - covered serial worker ordering and nonblocking coordinator start.
- [x] `bash` focused host tests and mode smoke runs - zero failures.

## Quality Checklist
- [x] Evidence-before-edit: target read complete; impact search used `rg` across callers; validation commands listed in task plan.
- [x] Existing pattern / reuse checked: extended existing Makefile trace flags, coordinator worker supervisor, and AirPlay test suite.
- [x] Contract understood: profiles only alter startup boundaries; no media or pairing semantics change.
- [x] Risk reviewed: compile contamination, worker lifecycle, and shutdown of partially started mDNS.
- [x] Mitigation recorded: clean profile builds, explicit compile errors, focused lifecycle tests.

## Validation Checklist
- [x] `make test-protocol-coordinator` exits 0.
- [x] `make test-airplay` exits 0.
- [x] `git diff --check` reports no errors.

## Test Checklist
- [x] Coordinator test proves serial services launch in order without blocking the caller.
- [x] Existing mDNS/receiver host smoke tests remain passing.

## Implementation Notes
The diagnostic profile is a Makefile whitelist. Profile zero preserves normal behavior; profiles one through eight force trace visibility and isolate AirPlay off, control-only, three mDNS boundaries, full parallel, full serial, and lower mDNS priority. Partial mDNS starts now own and close their socket even without a worker. The existing dirty worktree was not committed or stashed because doing so would capture unrelated user work.

## Files Changed
- `makefile`
- `source/main.c`
- `source/app/protocol_coordinator.h`
- `source/app/protocol_coordinator.c`
- `source/protocol/airplay/integration.c`
- `source/protocol/airplay/discovery/mdns.c`
- `scripts/test_protocol_coordinator.c`
