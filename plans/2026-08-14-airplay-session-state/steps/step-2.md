# Step 2: Receiver Identity Wiring

> Status: COMPLETED
> Created: 2026-08-14

## Goal
Wire logical identities through the AirPlay receiver and handler callbacks so connection closure follows logical remote-video lifetime.

## Prerequisites
- Step 1 completed — tested `AirPlaySessionManager` API exists.
- Files to modify: `source/protocol/airplay/protocol/rtsp.[ch]`, `source/protocol/airplay/receiver.c`, `source/protocol/airplay/protocol/handlers.c`, relevant tests.
- Pairing and FairPlay state must remain per TCP connection.

## Deliverables
- `AirPlayRtspSession` exposes separate connection and logical ids.
- Receiver observes/classifies requests before media routing and rejects contradictory/unbound remote-video requests.
- Last logical connection, not the first raw TCP close, ends remote URL playback.
- After this step: receiver/handler tests prove distinct connection and logical ids.

## Plan
- [x] `read` receiver/handler create, route, and close paths — map exact lifecycle before editing.
- [x] `rg` `session->id|session_closed|owner_session_id` in AirPlay source/tests — enumerate every identity consumer.
- [x] `edit` RTSP/receiver/handlers — propagate logical id and move logical-close policy to receiver.
- [x] `edit` handler/receiver tests — prove callback identity, shared-session close, and invalid route behavior.
- [x] `bash` `make test-airplay` — verify receiver, handlers, remote video, and prior tests.

## Quality Checklist
- [x] Evidence-before-edit: target read receiver/handlers, impact search identity consumers, validation `make test-airplay`
- [x] Existing pattern / reuse checked: Step 1 manager and existing receiver callback boundaries reused
- [x] Contract understood: per-connection security context remains isolated; only media owner identity is shared
- [x] Risk reviewed: premature stop, auth bypass, connection-kind conflict, cleanup leak
- [x] Mitigation recorded: route gate before handlers, last-reference close, regression tests

## Validation Checklist
- [x] `make test-airplay` exits 0
- [x] `git diff --check` exits 0

## Test Checklist
- [x] Handler callback receives logical id distinct from TCP id
- [x] First shared connection close does not stop remote video; final close does

## Implementation Notes
Added `logical_session_id` beside the raw RTSP connection id. Receiver classification now runs before pairing/handler routing, maps failures to deterministic HTTP/RTSP statuses, and owns last-reference remote-video cleanup. Handler media callbacks consume the logical id while pairing/FairPlay still use the per-connection session object. The first full regression exposed one missing direct include for the remote-video close API; it was added and the complete suite then passed.

## Files Changed
- `source/protocol/airplay/protocol/rtsp.h`
- `source/protocol/airplay/protocol/rtsp.c`
- `source/protocol/airplay/receiver.c`
- `source/protocol/airplay/protocol/handlers.c`
- `scripts/test_airplay_rtsp.c`
- `scripts/test_airplay_handlers.c`
- `makefile`
