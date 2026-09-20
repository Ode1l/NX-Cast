# Step 1: Aggregate Sender Connections

> Status: COMPLETED
> Created: 2026-08-22

## Goal
Associate concurrent RAOP and AirPlay HTTP/reverse connections from one sender and propagate generation-safe teardown intent.

## Prerequisites
- Runtime evidence identifies independent RAOP and HTTP logical IDs for one peer.
- Files to modify: `logical_session.h/.c`, `receiver.c`, remote-video lifecycle API, and focused session tests.
- Design: peer address supplements Apple Session ID without application-specific matching.

## Deliverables
- RAOP connections can be associated with the current logical AirPlay sender session.
- Close results distinguish ordinary close from explicit teardown intent.
- After this step: logical-session and remote-video focused tests pass.

## Plan
- [x] `edit` `scripts/test_airplay_session.c` — add same-peer RAOP plus HTTP/reverse aggregation and different-peer isolation cases.
- [x] `edit` `source/protocol/airplay/protocol/logical_session.h/.c` — accept peer identity, associate concurrent transports, and record teardown intent.
- [x] `edit` `source/protocol/airplay/receiver.c` and remote-video API — forward final close intent for the matching logical media session.
- [x] `bash` `make test-airplay-session && make test-airplay && git diff --check` — zero failures.

## Quality Checklist
- [x] Evidence-before-edit: read logical manager, receiver close path, remote close path; searched all callers and test targets
- [x] Existing pattern / reuse checked: extend `AirPlaySessionManager`, do not add a second registry
- [x] Contract understood: peer address associates transport; Apple Session ID remains authoritative for HTTP/reverse
- [x] Risk reviewed: wrong-device binding, pre-play teardown, stale generation, lock ordering
- [x] Mitigation recorded: peer isolation tests, media-active intent check, no callbacks under session lock

## Validation Checklist
- [x] `make test-airplay-session` exits 0
- [x] `git diff --check` exits 0

## Test Checklist
- [x] `make test-airplay` — aggregate suite including remote-video checks passes

## Implementation Notes
Concurrent connections are associated by peer address only as a supplement to the authoritative Apple Session ID. Explicit TEARDOWN intent is recorded on its logical session and forwarded only when that logical session closes.

## Files Changed
`source/protocol/airplay/protocol/logical_session.h`, `source/protocol/airplay/protocol/logical_session.c`, `source/protocol/airplay/receiver.c`, `source/protocol/airplay/media/remote_video.h`, `source/protocol/airplay/media/remote_video.c`, `scripts/test_airplay_session.c`.
