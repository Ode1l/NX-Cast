# Step 4: Supervise Protocol Lifecycle

> Status: COMPLETED
> Created: 2026-07-21

## Goal
Ensure IPTV, DLNA, and AirPlay startup/status/shutdown cannot block the main render/input loop and fail independently.

## Prerequisites
- Step 3 completed; protocols no longer need direct player access during lifecycle transitions.
- Existing coordinator state and service operation adapters are covered by host tests.

## Deliverables
- Coordinator uses a supervised worker/FSM rather than calling service start synchronously from `main`.
- IPTV heavy catalog work occurs in its worker; service start returns after local state/worker creation.
- After this step: a deliberately stalled service leaves main heartbeat/input and other services operational.

## Plan
- [x] `write` coordinator tests — simulate delayed/failing start, degraded readiness, stop during start, and idempotent shutdown.
- [x] `edit` coordinator/main lifecycle — enqueue lifecycle transitions and expose immutable snapshots to main.
- [x] `edit` IPTV/DLNA/AirPlay start contracts where needed — remove heavy or waiting work from supervisor calls.
- [x] `bash` focused tests, TSAN, full host regression, and strict Switch build.

## Quality Checklist
- [x] Evidence-before-edit: each service start/stop path and thread join read.
- [x] Existing pattern / reuse checked: extend current coordinator FSM instead of adding a second supervisor.
- [x] Contract understood: supervisor owns lifecycle; services own listeners/workers; main owns only snapshots.
- [x] Risk reviewed: stop/start overlap, orphan thread, partial start cleanup, unavailable network.
- [x] Mitigation recorded: idempotent operations, explicit stop-required state, fault-injection tests.

## Validation Checklist
- [x] Stalled-service test proves main-facing snapshot calls remain responsive.
- [x] Coordinator/full host tests and strict Switch build pass.

## Test Checklist
- [x] Failure, timeout, degraded startup, and stop-during-start tests pass under TSAN where available.

## Implementation Notes
Each enabled service now has one fixed lifecycle worker. Coordinator startup
only creates workers and returns; IPTV, DLNA, and AirPlay start independently.
Snapshots expose active worker and transition age. Tests prove a 150 ms stalled
IPTV start does not delay coordinator return or DLNA startup, and stop-during-
start/repeated stop remain safe under TSan.

## Files Changed
- `source/app/protocol_coordinator.c`
- `source/app/protocol_coordinator.h`
- `scripts/test_protocol_coordinator.c`
