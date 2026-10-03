# Step 1: Selective teardown

> Status: COMPLETED
> Created: 2026-10-04

## Goal
Honor stream selectors without treating every TEARDOWN as a session close.
## Prerequisites
- Handler, receiver, runtime, plist and UxPlay teardown read; callback usages searched.
## Deliverables
- Bounded selector parsing, selective runtime stop, callback wiring and regression cases.
## Plan
- [x] Edit handler to parse type 96/110 and log requested/remaining streams.
- [x] Wire selective stop through receiver and integration; preserve timing/keys for partial stop.
- [x] Test audio removal/re-add, video removal, full stop and malformed body.
- [x] Run existing host AirPlay suite.
## Quality Checklist
- [x] Evidence-before-edit and existing parser/worker reuse verified.
- [x] Review references, queue capacity and stream teardown races; prepare/commit under generation guard, release cancelled bridges outside runtime lock.
- [x] Full stop unchanged; receiver only terminates URL sessions on terminal (not scoped) teardown.
## Validation Checklist
- [x] Host compilation and git diff --check pass.
## Test Checklist
- [x] make test-airplay passes.
## Implementation Notes
All existing host AirPlay tests pass. Added scoped audio teardown/re-add, repeated teardown, malformed body, video-only teardown and full teardown cases. Runtime removal before a pending bridge is activated also passes. Actual selector values in latest log remain unknown until new diagnostics run.
## Files Changed
source/protocol/airplay/protocol/handlers.c and handlers.h; receiver.c and receiver.h; integration.c; media/mirror_runtime.c and mirror_runtime.h; media/stream_bridge.c and stream_bridge.h; scripts/test_airplay_handlers.c and test_airplay_mirror_runtime.c.
