# Step 4: Protocol And Concurrency Hardening

> Status: COMPLETED
> Created: 2026-08-16

## Goal
Close the video-relevant route and lifecycle gaps and prove reconnect, teardown, and protocol ownership remain deterministic under concurrency.

## Prerequisites
- Steps 1-3 completed with their focused tests passing.
- Files to modify: AirPlay route/session components, tests, and trace instrumentation only where evidence shows a gap.
- DLNA SSDP and IPTV remain discoverable while the coordinator arbitrates playback ownership.

## Deliverables
- Supported OPTIONS/GET/POST/PUT/SETUP/RECORD/FLUSH/TEARDOWN behavior is captured in a transcript matrix and returns stable status codes.
- Reverse, mirror, and remote-video state is removed exactly once on final logical-session teardown or server stop.
- Stress tests cover reconnect, stale action responses, simultaneous sends, stop during traffic, and coordinator replacement.

## Plan
- [x] `read/search` implemented routes against the UxPlay behavior matrix and fill only video-relevant gaps.
- [x] `edit` route/session lifecycle code — make teardown and ownership transitions idempotent and observable.
- [x] `write` protocol transcript and concurrency stress tests with bounded iteration counts and deadlines.
- [x] `bash` run focused stress tests repeatedly and then `make test-airplay`.

## Quality Checklist
- [x] Evidence-before-edit: route matrix from local code and UxPlay reference; validation baseline from Steps 1-3
- [x] Existing pattern / reuse checked: `logical_session`, `protocol_coordinator`, mirror command queue, reverse send lock
- [x] Contract understood: final logical connection owns media teardown; stale generations cannot control the player
- [x] Risk reviewed: deadlock, race, starvation, behavior regression, observability
- [x] Mitigation recorded: fixed lock ordering, bounded waits, idempotent close, repeatable stress tests

## Validation Checklist
- [x] `git diff --check` exits 0
- [x] Protocol compatibility matrix has no undocumented implemented route

## Test Checklist
- [x] Focused transcript/concurrency tests pass repeatedly
- [x] `make test-airplay` passes

## Implementation Notes
`/reverse` participates in logical-session binding but not in pairing authorization classification, preserving the sender's observed connection order. A replacement reverse connection shuts down the stale socket under the registry lock, while each active connection keeps a dedicated send mutex. The route matrix fixes existing behavior rather than adding a second protocol state machine; `FLUSH` remains an acknowledgement and never calls mpv from a network worker.

## Files Changed
- `source/protocol/airplay/protocol/logical_session.c`
- `source/protocol/airplay/server.c`
- `scripts/test_airplay_session.c`
- `scripts/test_airplay_server_lifecycle.c`
- `scripts/test_airplay_handlers.c`
- `docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md`
