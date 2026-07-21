# Step 3: Protocol Media Transaction Migration

> Status: COMPLETED
> Created: 2026-07-21

## Goal
Route DLNA, IPTV, and AirPlay renderer mutations through coordinator transactions and verify cross-protocol concurrency and playback regressions.

## Prerequisites
- Step 2 completed — coordinator owns application protocol lifecycle.
- Files to modify: `source/iptv/iptv.c`, `source/player/core/session.c`, `source/protocol/dlna/control/action/avtransport.c`, `source/protocol/dlna/control/action/renderingcontrol.c`, `source/protocol/dlna/control/handler.c`, `source/protocol/airplay/integration.c`, coordinator tests, and plan records.
- Existing renderer behavior and latest-request-wins policy remain unchanged.

## Deliverables
- No protocol module directly begins/ends ownership transitions or claims new player ownership.
- All renderer-changing callbacks validate a coordinator transaction or lease before mutation.
- Final host, concurrency, AirPlay, formatting, and strict Switch build checks pass.
- After this step: adding a future protocol requires one owner value plus coordinator lifecycle/media adapters, not new pairwise exclusion logic.

## Plan
- [x] `read` each direct ownership call site in IPTV, DLNA, and AirPlay — preserve exact renderer call ordering and failure cleanup.
- [x] `edit` protocol call sites and `source/app/protocol_coordinator.*` — replace direct claims/transition locks with coordinator begin/validate/end/release APIs.
- [x] `edit` `source/protocol/dlna/control/handler.c` — suppress renderer events owned by IPTV/AirPlay so DLNA state and subscribers do not consume another protocol's session.
- [x] `edit` `source/player/core/session.c` and DLNA handler lifecycle — keep shared player init/deinit and ownership reset under main/coordinator control rather than event callback registration.
- [x] `edit` `scripts/test_protocol_coordinator.c` and ownership tests if needed — stress concurrent takeover and stale callback rejection.
- [x] `bash` `make test-protocol-coordinator && make test-airplay` — expect all host regressions to pass.
- [x] `bash` ThreadSanitizer coordinator/ownership test — expect no data races where supported.
- [x] `bash` strict Switch trace build and `git diff --check` — expect successful NRO and clean formatting.
- [x] `edit` plan and step files — record exact files, commands, results, and remaining physical-device validation risk.

## Quality Checklist
- [x] Evidence-before-edit: target reads for all ownership callers, impact search `rg player_ownership_ source`, validation full host/build suite.
- [x] Existing pattern / reuse checked: renderer interfaces and ownership leases are reused; no second locking implementation is introduced.
- [x] Contract understood: transaction begin serializes takeover; lease generation rejects stale callbacks; transaction end always releases the lock.
- [x] Risk reviewed: deadlock, lock leak on error branches, stale AirPlay callbacks, DLNA control semantics, IPTV deinit.
- [x] Mitigation recorded: structured cleanup on every branch, host stress test, TSAN, strict Switch link/build.

## Validation Checklist
- [x] `make test-protocol-coordinator` exits 0
- [x] `make test-airplay` exits 0
- [x] ThreadSanitizer test exits 0 or unsupported environment is documented
- [x] Strict Switch trace build exits 0
- [x] `git diff --check` exits 0

## Test Checklist
- [x] Coordinator tests cover simultaneous owner requests and stale lease rejection.
- [x] Existing AirPlay, mirror, remote-video, and ownership tests all pass.
- [x] Physical Switch multi-protocol handoff remains a documented follow-up because hardware validation is unavailable in this environment.

## Implementation Notes
Replaced protocol-level transition mutex, claim, validation, and release calls with coordinator transactions, exact-generation lease guards, unowned-or-DLNA compatibility guards, and teardown barriers. DLNA renderer reads now use guarded snapshots, and its event callback ignores active IPTV/AirPlay sessions. During review, `soap_handler_shutdown()` was found deinitializing the global player and `player_set_event_callback()` was resetting ownership; both lifecycle leaks were removed so only `main.c` initializes/deinitializes the shared player. Service operations were generalized into an indexed table with forward startup and reverse shutdown order. All planned validation passed. Residual risk is physical Switch handoff behavior across live DLNA/IPTV/AirPlay streams, which requires device testing.

## Files Changed
- `source/app/protocol_coordinator.h`
- `source/app/protocol_coordinator.c`
- `source/iptv/iptv.c`
- `source/player/core/session.c`
- `source/protocol/airplay/integration.c`
- `source/protocol/dlna/control/action/avtransport.c`
- `source/protocol/dlna/control/action/renderingcontrol.c`
- `source/protocol/dlna/control/handler.c`
- `scripts/test_protocol_coordinator.c`
