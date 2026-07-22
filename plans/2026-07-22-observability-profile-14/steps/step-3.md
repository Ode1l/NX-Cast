# Step 3: DLNA/libmpv Sampling and End-to-End Validation

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Add bounded DLNA/libmpv cache and video observations, wire resource boundary snapshots, document the test procedure, and produce a verified Profile 14 build.

## Prerequisites
- Steps 1 and 2 completed with runtime and AirPlay diagnostics available.
- Files to modify: `source/player/backend/libmpv.c`, ownership boundaries, docs, and any final test fixtures.

## Deliverables
- DLNA active loading/buffering/seeking emits at most one diagnostic sample per second with cache, HTTP/Range, seek, and video state.
- Claim, stop, end-file, and next-load boundaries emit compact resource snapshots.
- Documentation tells the user exactly how to run and interpret Profile 14.
- Available focused host tests and a clean Profile 14 Switch build pass; aggregate host-suite environment limitations are documented.

## Plan
- [x] `read/rg` PlayerMedia source ownership, libmpv event/property lifetime, and stop/end/load boundaries.
- [x] `edit` libmpv and coordinator boundaries with best-effort, non-failing observations.
- [x] `edit` documentation and test picker for the Profile 14 workflow.
- [x] `bash` attempt the full AirPlay host suite, parse JSON, run focused host tests, and clean-build Switch Profile 14.

## Quality Checklist
- [x] Evidence-before-edit: libmpv contracts and owner boundaries documented.
- [x] Existing pattern / reuse checked: existing observed properties, event loop, log filtering, and coordinator claims.
- [x] Contract understood: missing properties/log metadata must never alter playback.
- [x] Risk reviewed: node lifetime, log leakage, sampling overhead, cross-thread access.
- [x] Mitigation recorded: consume/free property nodes immediately, omit URLs, 1 Hz active-state gate, atomics/owner checks.

## Validation Checklist
- [x] `.vscode/tasks.json` parses successfully.
- [x] Profile 14 Switch build exits zero and produces `NX-Cast.nro`.
- [x] Diff/source review confirms no Profile 13 behavior flags or player options changed.

## Test Checklist
- [x] Full `make ... test-airplay` was attempted; unrelated strict-C11 `strdup` and missing Cygwin mbedTLS/FFmpeg development packages prevent completing that aggregate target.
- [x] Focused runtime/network/coordinator/server/mDNS tests pass with strict warnings-as-errors.

## Implementation Notes
- Added a Profile-gated runtime logging wrapper so Profile 13 never collects snapshots.
- The DLNA sampler checks current ownership and only reads properties during loading, buffering, or seeking; the monotonic gate permits at most one line per second.
- `demuxer-cache-state` is read and freed within the sample. Missing cache/video/HTTP fields are rendered as `unknown`, and no media URL is included.
- HTTP status parsing only accepts `HTTP/`, `HTTP status`, or `HTTP error` contexts to avoid treating URL/IP digits as status codes.
- Clean Profile 14 build passed. Final NRO: 25,600,698 bytes, SHA-256 `7DC90E21B382011AF824376906B124C6AFC4543B3B2B61277368FD228CB1B2B5`.

## Files Changed
- `source/app/runtime_observability.h`
- `source/app/protocol_coordinator.c`
- `source/player/backend/libmpv.c`
- `.vscode/tasks.json`
- `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md`
- `plans/2026-07-22-observability-profile-14/plan.md`
- `plans/2026-07-22-observability-profile-14/steps/step-3.md`
