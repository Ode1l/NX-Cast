# Step 2: Main Lifecycle Integration

> Status: COMPLETED
> Created: 2026-07-21

## Goal
Make `main.c` delegate protocol startup, asynchronous status reconciliation, and ordered shutdown to the coordinator.

## Prerequisites
- Step 1 completed — coordinator state contract and host tests pass.
- Files to modify: `source/main.c`, `source/app/protocol_coordinator.c`, `source/app/protocol_coordinator.h`, `source/protocol/airplay/integration.c`, `source/protocol/airplay/integration.h`, `scripts/test_protocol_coordinator.c`.
- Existing startup dependencies remain ordered: storage/network/player readiness precede network protocol listeners.

## Deliverables
- One coordinator start call replaces direct IPTV/DLNA/AirPlay startup in `main.c`.
- Main-loop tick/status snapshot replaces scattered protocol booleans and AirPlay polling.
- One coordinator shutdown call enforces AirPlay, DLNA, then IPTV teardown before player/network teardown.
- After this step: host lifecycle tests and strict Switch compilation pass.

## Plan
- [x] `read` `source/main.c` startup, frame status, home-status, and shutdown consumers — enumerate direct booleans before replacement.
- [x] `edit` `source/app/protocol_coordinator.*` — add concrete service lifecycle effects and asynchronous AirPlay reconciliation.
- [x] `edit` `source/protocol/airplay/integration.*` — expose the existing `starting` state so the coordinator can distinguish pending startup from terminal failure without parsing status strings.
- [x] `edit` `source/main.c` — compose coordinator config, tick once per frame, consume its snapshot, and call ordered shutdown.
- [x] `edit` `scripts/test_protocol_coordinator.c` — add operation-order and partial-failure assertions through injected service operations.
- [x] `bash` `make test-protocol-coordinator` and strict Switch build — expect no lifecycle regressions or unresolved symbols.

## Quality Checklist
- [x] Evidence-before-edit: target read `source/main.c`, impact search for lifecycle APIs, validation host test plus strict Switch build.
- [x] Existing pattern / reuse checked: current shutdown order and AirPlay status API are preserved behind the coordinator.
- [x] Contract understood: `main.c` owns coordinator tick timing; coordinator owns protocol service side effects; player and network shutdown stay outside it.
- [x] Risk reviewed: startup failure, async AirPlay race, shutdown during startup, status/UI regression.
- [x] Mitigation recorded: injected-op order tests, idempotent shutdown, per-service degraded states, strict cross-build.

## Validation Checklist
- [x] `make test-protocol-coordinator` exits 0
- [x] Strict Switch trace build exits 0
- [x] `git diff --check` exits 0

## Test Checklist
- [x] Coordinator operation-order tests pass for full, network-disabled, and AirPlay-start-failure paths.

## Implementation Notes
Added injected service operations and a runtime lifecycle that starts IPTV, DLNA, and AirPlay in order, polls asynchronous AirPlay state, tolerates partial failures, and stops AirPlay, DLNA, then IPTV. `main.c` now supplies thin adapters and consumes one coordinator snapshot for home status and shutdown diagnostics. AirPlay integration exposes its existing `starting` flag. `make test-protocol-coordinator`, `git diff --check`, and the strict trace Switch build all passed; the generated NRO was 25,109,178 bytes.

## Files Changed
- `source/app/protocol_coordinator.h`
- `source/app/protocol_coordinator.c`
- `source/main.c`
- `source/protocol/airplay/integration.h`
- `source/protocol/airplay/integration.c`
- `scripts/test_protocol_coordinator.c`
