# Step 2: Implement AirPlay Takeover Cleanup

> Status: COMPLETED
> Created: 2026-08-17

## Goal
Provide a synchronous AirPlay integration entry point that stops retained remote-video or mirror playback, releases the coordinator owner, and clears the integration lease for cross-protocol takeover.

## Prerequisites
- Step 1 completed.
- Files to modify: `source/protocol/airplay/integration.h`, `source/protocol/airplay/integration.c`, `source/main.c`.

## Deliverables
- `airplay_integration_release_active_media()` stops the matching player path and synchronously releases the lease.
- `main_protocol_airplay_release_active_media()` wires the new coordinator callback.
- Strict Switch trace build succeeds with the callback wired.

## Plan
- [ ] `read` source/protocol/airplay/integration.h — inspect public API and ownership types.
- [ ] `read` source/protocol/airplay/integration.c — inspect remote/mirror lease storage and stop callbacks.
- [ ] `edit` source/protocol/airplay/integration.h — add the active-media release declaration.
- [ ] `edit` source/protocol/airplay/integration.c — implement lease validation, player stop submission, synchronous coordinator release, and integration lease clearing.
- [ ] `edit` source/main.c — set `.airplay_release_active_media` on the protocol operations.
- [ ] `bash` make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1 — expect exit 0.

## Quality Checklist
- [ ] Evidence-before-edit: integration and main wiring read; impact search `airplay_integration_stop_active_media`; validation trace build.
- [ ] Existing pattern / reuse checked: existing remote/mirror stop and `protocol_coordinator_media_release`.
- [ ] Contract understood: accepts only a matching current AirPlay lease and never releases a different owner.
- [ ] Risk reviewed: shutdown / takeover / stale generation.
- [ ] Mitigation recorded: exact lease comparison plus coordinator revalidation in Step 1.

## Validation Checklist
- [ ] `git diff --check` exits 0

## Test Checklist
- [ ] Switch integration has no host unit binary; full trace build is the focused validation in this step.

## Implementation Notes
- Added `airplay_integration_release_active_media()` for exact AirPlay remote-video or mirror leases.
- The takeover path enqueues `STOP_ANY`, unbinds a mirror bridge when present, synchronously releases the coordinator lease, then clears the integration lease.
- Wired the new coordinator callback in `main.c`; strict full-trace Switch build passes.

## Files Changed
- `source/protocol/airplay/integration.h`
- `source/protocol/airplay/integration.c`
- `source/main.c`
