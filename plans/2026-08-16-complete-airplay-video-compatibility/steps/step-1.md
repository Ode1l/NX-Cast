# Step 1: Reverse Transport

> Status: COMPLETED
> Created: 2026-08-16

## Goal
Provide a bounded, lifecycle-safe PTTH reverse connection that handlers can use to send AirPlay events by logical session.

## Prerequisites
- Baseline `make test-airplay` passes.
- Files to modify: `source/protocol/airplay/server.*`, `source/protocol/airplay/protocol/rtsp.*`, `source/protocol/airplay/handlers.*`, and focused host tests.
- Design confirmed: keep each client worker as receive owner and serialize all writes with one connection-local mutex.

## Deliverables
- `/reverse` returns the required protocol-switch response and registers the connection role after the response is sent.
- A bounded outbound HTTP request encoder and server reverse-send API fail cleanly during disconnect or shutdown.
- After this step: focused reverse transport tests and `make test-airplay` pass.

## Plan
- [x] `edit` `protocol/rtsp.*` — add validated outbound request encoding and explicit reverse-upgrade response metadata.
- [x] `edit` `server.*` — add per-client send serialization, logical-session reverse registration, lookup, send, and teardown.
- [x] `edit` `handlers.*` — implement `/reverse` without giving handlers socket ownership.
- [x] `write` focused host tests — cover upgrade, event send, response consumption, disconnect, and shutdown.
- [x] `bash` `make test-airplay` — 0 failures.

## Quality Checklist
- [x] Evidence-before-edit: target read `server.c`, `rtsp.c`, `handlers.c`; impact search `rg "AirPlayRtspResponse|airplay_server"`; validation `make test-airplay`
- [x] Existing pattern / reuse checked: bounded request parsing in `protocol/rtsp.c` and logical sessions in `protocol/logical_session.c`
- [x] Contract understood: worker owns reads; send API owns no socket lifetime; all payloads are length-bounded
- [x] Risk reviewed: concurrency, resource lifecycle, protocol framing, security
- [x] Mitigation recorded: static client lifetime, mutex-protected writes, active-session checks, transcript tests

## Validation Checklist
- [x] `git diff --check` exits 0
- [x] Reverse transport host target compiles with warnings as errors

## Test Checklist
- [x] Focused reverse transport tests pass
- [x] `make test-airplay` passes

## Implementation Notes
Implemented HTTP/1.1 101/PTTH upgrade metadata, a validated one-shot outbound request encoder, and a server reverse-send API. Each client retains read ownership while all writes are serialized by a connection-local mutex. Upgraded connections consume dedicated reverse HTTP responses rather than feeding them to the request parser. Host tests cover two event sends, response consumption, peer disconnect, server shutdown, and diagnostics balance.

## Files Changed
- `source/protocol/airplay/protocol/rtsp.h`
- `source/protocol/airplay/protocol/rtsp.c`
- `source/protocol/airplay/server.h`
- `source/protocol/airplay/server.c`
- `source/protocol/airplay/protocol/handlers.c`
- `scripts/test_airplay_rtsp.c`
- `scripts/test_airplay_handlers.c`
- `scripts/test_airplay_server_lifecycle.c`
