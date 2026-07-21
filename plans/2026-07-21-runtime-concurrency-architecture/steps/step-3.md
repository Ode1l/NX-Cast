# Step 3: Migrate Playback Producers

> Status: COMPLETED
> Created: 2026-07-21

## Goal
Route UI, IPTV, DLNA, and AirPlay playback requests through the Media Actor so no producer thread directly mutates libmpv state.

## Prerequisites
- Step 2 completed with a tested actor command API.
- Direct-call inventory from Step 1 is the migration checklist.

## Deliverables
- Main/UI uses nonblocking submissions and observes snapshots for completion.
- DLNA uses bounded acknowledgements; IPTV and AirPlay use accepted/rejected async requests with generation leases.
- After this step: scoped search finds no renderer/player mutation outside player core compatibility tests.

## Plan
- [x] `edit` UI/IPTV paths — submit asynchronous commands without blocking render/input.
- [x] `edit` DLNA/AirPlay paths — submit source-tagged commands with bounded protocol responses.
- [x] `edit` ownership/coordinator integration — move active source/generation arbitration into actor command acceptance.
- [x] `write` cross-source tests — cover takeover, stale command rejection, stop/load ordering, and callback after teardown.
- [x] `bash` focused/full host tests, sanitizers, call-boundary `rg`, and strict Switch build.

## Quality Checklist
- [x] Evidence-before-edit: every producer and callback caller identified.
- [x] Existing pattern / reuse checked: one actor API, no protocol-specific queues.
- [x] Contract understood: parsing stays in protocol threads; only normalized commands cross the boundary.
- [x] Risk reviewed: protocol timeout behavior, takeover races, duplicate callbacks, UI responsiveness.
- [x] Mitigation recorded: generation tests, bounded waits off main, observable rejection reasons.

## Validation Checklist
- [x] No production direct player mutation remains outside player core.
- [x] Full host suite and strict Switch build pass.

## Test Checklist
- [x] Cross-source concurrency and stale-generation tests pass under sanitizers.

## Implementation Notes
Added the normalized public player-command API with source identity and ownership lease metadata. UI/main/IPTV/AirPlay now enqueue asynchronously; DLNA SOAP waits at most 750 ms for command execution. The Actor validates leases both before enqueue and immediately before execution, so a takeover rejects stale queued callbacks. AirPlay stream-bridge binding now crosses the Actor with retain/release ownership, and lease release is FIFO-ordered behind stop/unbind. Coordinator guards no longer hold the global transition lock across player submission. The full AirPlay host suite, ASan/UBSan, TSan, direct-call audit, and strict Switch build pass.

## Files Changed
- `source/player/player.h`
- `source/player/renderer.h`
- `source/player/core/media_actor.h`
- `source/player/core/media_actor.c`
- `source/player/core/session.c`
- `source/player/ui/controls.c`
- `source/app/protocol_coordinator.h`
- `source/app/protocol_coordinator.c`
- `source/iptv/iptv.c`
- `source/protocol/dlna/control/action/avtransport.c`
- `source/protocol/dlna/control/action/renderingcontrol.c`
- `source/protocol/airplay/integration.c`
- `source/main.c`
- `scripts/test_media_actor.c`
