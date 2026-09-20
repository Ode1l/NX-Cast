# Step 1: Add And Test Coordinator Takeover Contract

> Status: COMPLETED
> Created: 2026-08-17

## Goal
Extend the coordinator so a cross-resource claim can request synchronous release of an AirPlay owner without weakening existing same-resource or non-AirPlay exclusivity.

## Prerequisites
- Corrected plan approved.
- Files to modify: `source/app/protocol_coordinator.h`, `source/app/protocol_coordinator.c`, `scripts/test_protocol_coordinator.c`.

## Deliverables
- `ProtocolCoordinatorOperations` exposes an optional `airplay_release_active_media` callback.
- `protocol_coordinator_media_begin()` invokes and revalidates that callback before replacing an AirPlay owner with a different resource mode.
- Focused host tests cover successful takeover, callback failure, same-resource replacement, and stale-release races.

## Plan
- [ ] `read` source/app/protocol_coordinator.h — inspect operation struct and media transaction API.
- [ ] `read` source/app/protocol_coordinator.c — inspect `operations_valid` and `protocol_coordinator_media_begin`.
- [ ] `edit` source/app/protocol_coordinator.h — add `bool (*airplay_release_active_media)(const PlayerOwnershipLease *lease, void *context);`.
- [ ] `edit` source/app/protocol_coordinator.c — require the callback when exclusive resources and AirPlay are enabled; invoke it only for cross-resource AirPlay owners and re-read ownership after return.
- [ ] `edit` scripts/test_protocol_coordinator.c — add fake callback counters and takeover lifecycle tests.
- [ ] `bash` make test-airplay — expect exit 0.

## Quality Checklist
- [ ] Evidence-before-edit: coordinator header/source/test read; impact search `protocol_coordinator_media_begin`; validation `make test-airplay`.
- [ ] Existing pattern / reuse checked: optional `ProtocolCoordinatorOperations` callbacks and lease revalidation.
- [ ] Contract understood: callback is synchronous, nonblocking, AirPlay-only, and must release the exact previous lease.
- [ ] Risk reviewed: concurrency / ownership / exclusivity.
- [ ] Mitigation recorded: callback failure rejects; callback stale lease cannot release the new owner.

## Validation Checklist
- [ ] `git diff --check` exits 0

## Test Checklist
- [ ] `make test-airplay` — all pass

## Implementation Notes
- Added the optional AirPlay release callback to `ProtocolCoordinatorOperations`; exclusive builds with AirPlay enabled require it.
- `protocol_coordinator_media_begin()` invokes the callback before taking the ownership transition lock, then re-reads ownership to catch races.
- Fake callback tests cover same-resource replacement, failed takeover, successful takeover, and stale-owner preservation.

## Files Changed
- `source/app/protocol_coordinator.h`
- `source/app/protocol_coordinator.c`
- `scripts/test_protocol_coordinator.c`
