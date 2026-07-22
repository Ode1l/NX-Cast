# Step 1: Support AirPlay Audio-First Setup

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Accept audio-only SETUP before mirror SETUP while preserving a single bridge and complete TEARDOWN cleanup.

## Prerequisites
- Plan clarification is complete and the AirPlay setup-order decision is confirmed.
- Files to modify: `scripts/test_airplay_handlers.c`, `scripts/test_airplay_mirror_runtime.c`, `source/protocol/airplay/protocol/handlers.c`, `source/protocol/airplay/media/mirror_runtime.c`.
- Existing combined mirror/audio SETUP and mirror-first runtime tests remain the compatibility baseline.

## Deliverables
- Handler and runtime regression tests for initial SETUP → RECORD → audio SETUP → mirror SETUP → TEARDOWN.
- Runtime support for pending audio format/receiver state and cleanup without a mirror session.
- After this step: AirPlay host handler/runtime tests accept both mirror-first and audio-first construction orders.

## Plan
- [x] `edit` `scripts/test_airplay_handlers.c` — add a failing split audio-first/mirror-later SETUP sequence and assert one deferred record/stop.
- [x] `edit` `scripts/test_airplay_mirror_runtime.c` — add a failing transport → audio → mirror → record/stop lifecycle plus audio-only stop cleanup cycle.
- [x] `bash` the existing AirPlay handler/runtime host compile/run commands from `makefile` — dependency discovery proved the current host cannot link these tests before reaching assertions.
- [x] `edit` `source/protocol/airplay/protocol/handlers.c` — remove the mirror-first precondition for valid type 96 SETUP while retaining duplicate/session validation.
- [x] `edit` `source/protocol/airplay/media/mirror_runtime.c` — retain audio format before bridge creation, configure it during mirror open, and clean every partial transport resource on stop.
- [x] `bash` Profile 13 incremental build — confirm the modified production sources compile and link for the Switch target.

## Quality Checklist
> Evidence summary only. Detailed guidance lives in `references/code-quality.md`, `references/risk-classification.md`, and `references/verify-step.md`.

- [x] Evidence-before-edit: target read `handlers.c`/`mirror_runtime.c`, impact search `rg "mirror_setup|audio_open|runtime_stop"`, validation attempted `make test-airplay` plus target build substitute
- [x] Existing pattern / reuse checked: extend existing handler/runtime tests and retain the existing shared `AirPlayStreamBridge`
- [x] Contract understood: valid setup returns ports; duplicate/wrong-session setup fails; RECORD is deferred until mirror exists; TEARDOWN is idempotent
- [x] Risk reviewed: correctness / API-contract / performance / observability
- [x] Mitigation recorded: both setup orders remain supported, audio-only teardown is covered, bridge ownership and asynchronous player commands remain unchanged

## Validation Checklist
- [x] Modified production and test-path production dependencies compile/link in the Profile 13 Switch build.
- [x] `make test-airplay` was attempted and stopped only at pre-existing missing host mbedTLS/FFmpeg discovery; residual host-test execution risk is recorded.

## Test Checklist
- [x] `scripts/test_airplay_handlers.c` — split audio-first setup asserts successful ports and records exactly once.
- [x] `scripts/test_airplay_mirror_runtime.c` — audio-first bridge binding and audio-only cleanup cases are present; execution awaits a dependency-capable host.

## Implementation Notes
Changed the handler so type 96 no longer depends on an already-established type 110 stream. The runtime now creates the audio receiver after transport preparation, stores its validated format when no bridge exists, configures the shared bridge during later mirror open, and propagates recording when audio arrives late. Stop now destroys timing/audio/mirror/bridge resources even when no mirror media session was created. Existing direct mirror-open behavior was preserved. `make test-airplay` could not compile on this machine because host mbedTLS/FFmpeg packages are absent; two incremental Profile 13 builds compiled and linked the production changes. A protective commit was intentionally skipped because the dirty worktree contains user/prior changes.

## Files Changed
- `scripts/test_airplay_handlers.c`
- `scripts/test_airplay_mirror_runtime.c`
- `source/protocol/airplay/protocol/handlers.c`
- `source/protocol/airplay/media/mirror_runtime.c`
