# Step 1: Logical Session Registry

> Status: COMPLETED
> Created: 2026-08-14

## Goal
Create a small, thread-safe AirPlay connection classifier and exact logical-session registry with focused host tests and a documented behavior contract.

## Prerequisites
- User confirmed an independent C implementation based on reference behavior rather than a source port.
- Files to modify: `source/protocol/airplay/protocol/logical_session.[ch]`, `scripts/test_airplay_session.c`, `makefile`, `docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md`.
- Existing global coordinator remains unchanged.

## Deliverables
- Fixed-capacity registry classifies RAOP, AirPlay HTTP, and BLE connections and binds exact bounded Apple session IDs.
- Tests cover sharing, conflict, unbound remote routes, close refcounts, capacity, and invalid input.
- After this step: `make test-airplay-session` passes independently.

## Plan
- [x] `read` `source/protocol/airplay/protocol/rtsp.[ch]` and `makefile` — confirm request/header and host-thread conventions.
- [x] `rg` `X-Apple-Session-ID|client_session_id|session manager` in source/tests/reference paths — verify no reusable registry exists and capture route rules.
- [x] `write` `source/protocol/airplay/protocol/logical_session.[ch]` — add the smallest fixed-capacity, mutex-protected registry.
- [x] `write` `scripts/test_airplay_session.c` — add boundary and lifetime tests before integration.
- [x] `edit` `makefile` — add a focused target and include it in `test-airplay`.
- [x] `write` `docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md` — record observed connection classes, identity, stages, and non-goals.
- [x] `bash` `make test-airplay-session` — expect all focused tests to pass.

## Quality Checklist
- [x] Evidence-before-edit: target read `rtsp.[ch]`, impact search `rg X-Apple-Session-ID`, validation `make test-airplay-session`
- [x] Existing pattern / reuse checked: existing RTSP parser and platform mutex patterns inspected; no logical-session registry found
- [x] Contract understood: bounded request metadata in, classification/snapshot/close result out; no network or player side effects
- [x] Risk reviewed: concurrency, identity collision, malformed headers, fixed-capacity exhaustion
- [x] Mitigation recorded: exact compare, bounded storage, mutex, fail-closed results, focused tests

## Validation Checklist
- [x] `make test-airplay-session` exits 0
- [x] `git diff --check -- source/protocol/airplay/protocol/logical_session.c source/protocol/airplay/protocol/logical_session.h scripts/test_airplay_session.c makefile docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md` exits 0

## Test Checklist
- [x] `make test-airplay-session` — all classification, binding, close, and rejection tests pass

## Implementation Notes
Added an opaque fixed-capacity manager using the project's Switch/pthread mutex convention. It stores exact bounded Apple session ids privately, exposes only generated logical tokens, and keeps RAOP/BLE connection-scoped. Focused tests passed normally and under AddressSanitizer/UndefinedBehaviorSanitizer (`detect_leaks=0`; macOS host does not support sanitizer leak detection).

## Files Changed
- `source/protocol/airplay/protocol/logical_session.h`
- `source/protocol/airplay/protocol/logical_session.c`
- `scripts/test_airplay_session.c`
- `docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md`
- `makefile`
