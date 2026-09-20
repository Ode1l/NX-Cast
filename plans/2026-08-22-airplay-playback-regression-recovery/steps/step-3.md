# Step 3: Enforce One Media Session State Machine

> Status: COMPLETED
> Created: 2026-08-22

## Goal
Make one protocol-neutral reducer authoritative for preparation, activation, attachment, replacement, failure, Stop, and release.

## Prerequisites
- User explicitly prioritized the state-machine boundary before transport isolation; Step 2 remains an independent later hardening step.
- Files: remote video, AirPlay integration, coordinator, and existing tests.
- Design: callbacks submit typed events; only the reducer mutates authority; effects execute after unlocking.

## Deliverables
- Accepted `/play` reserves one generation before dependent commands are acknowledged.
- Every terminal path releases exactly once.
- A bounded `ProtocolMediaEvent` envelope identifies event kind, owner, token, generation, timestamp, and a small event-specific payload or descriptor handle.
- A reducer models lifecycle, playback, and control attachment as orthogonal fields and returns an explicit transition result; existing adapters execute effects after unlocking.
- After this step: all protocols share transitions; direct URL and Reverse HLS differ only in descriptor/preparation events.

## Plan
- [x] `rg` `protocol_coordinator_media_|player_ownership_|owner_claimed|generation` — inventoried direct authority mutation and separated lease storage from media-session lifecycle.
- [x] `edit` coordinator tests — added one black-box sequence for preparing, loading, active, detached, replacement, paused, stale terminal, and release.
- [x] `edit` coordinator header/source — added the bounded event/result contract and one dispatch path with stale generation rejection.
- [x] `edit` coordinator source — implemented a deterministic reducer with no I/O, allocation, logging, waiting, or callback execution under its state mutex.
- [x] `edit` protocol adapters and `main.c` — routed ownership synchronization and player observations through typed events while preserving legacy begin/release APIs.
- [x] `edit` effect boundary — retained the existing `MediaActor` and coordinator adapters outside the reducer lock instead of adding a duplicate effect queue.
- [x] `edit` AirPlay integration — submitted loading, Stop, control-attached, and control-detached events without moving HLS parsing state into the shared reducer.
- [x] `bash` `make test-protocol-coordinator && make test-airplay && make full-trace-build BUILD_JOBS=4 && git diff --check` — all passed.

## Quality Checklist
- [x] Evidence-before-edit: inventoried mutations, searched callers, and ran transition tests
- [x] Existing pattern / reuse checked: reused `MediaActor`; no global bus or duplicate player queue added
- [x] Contract understood: one event, one guarded transition, one snapshot and explicit result
- [x] Risk reviewed: stale command, double release, deadlock, takeover
- [x] Mitigation recorded: black-box sequence, generation checks, idempotent transitions, and no effects under state mutex

## Validation Checklist
- [x] `make test-protocol-coordinator` exits 0
- [x] `git diff --check` exits 0

## Test Checklist
- [x] `make test-protocol-coordinator` — black-box sequence follows expected transitions and rejects stale terminal events
- [x] `make test-airplay` — all existing AirPlay host checks pass

## Implementation Notes
Implemented a small pure reducer instead of a second actor/event-bus framework. `PlayerOwnershipLease` remains the generation allocator and validation primitive; the coordinator owns the media-session snapshot and translates existing begin/release/player/AirPlay callbacks into events. Reducer calls never perform I/O or blocking work. The existing player actor remains the player side-effect executor. Full-trace logs now include media lifecycle, playback, and control attachment.

## Files Changed
- `source/app/protocol_media_session.h`
- `source/app/protocol_media_session.c`
- `source/app/protocol_coordinator.h`
- `source/app/protocol_coordinator.c`
- `source/main.c`
- `source/protocol/airplay/integration.c`
- `scripts/test_protocol_coordinator.c`
- `makefile`
