# Step 1: Late audio and first-frame diagnostics

> Status: COMPLETED
> Created: 2026-09-24

## Goal
Keep a mirror session alive when audio arrives after RECORD and identify the first invalid decrypted packet's framing.

## Prerequisites
- Latest log, runtime callback, packet parser, and local reference inspected.

## Deliverables
- Late audio callback accepts the active mirror session.
- Invalid AU logs bounded framing metadata, not keys or full media payloads.
- Regression test and Switch build pass.

## Plan
- [x] Edit runtime audio RECORD callback for active mirror sessions.
- [x] Add focused late-audio runtime test.
- [x] Log first invalid AU length/framing fields in mirror session.
- [x] Run host AirPlay tests, Switch build, and diff check.

## Quality Checklist
- [x] Evidence-before-edit: target files and callers read; validation commands identified.
- [x] Existing pattern / reuse checked: existing observability macro and runtime test reused.
- [x] Contract understood: audio-only path unchanged; mirror path only marks audio recording.
- [x] Risk reviewed: session ownership and media-data leakage.
- [x] Mitigation recorded: session check, no secrets or full payload in logs, tests.

## Validation Checklist
- [x] `make RELEASE_JOBS=4 release-build`
- [x] `git diff --check`

## Test Checklist
- [x] `make test-airplay`

## Implementation Notes
The new trace line reports first NAL length/type and boundary fit on rejected access units. No cryptographic behavior was changed; device validation is still required.

## Files Changed
- `source/protocol/airplay/media/mirror_runtime.c`
- `source/protocol/airplay/mirror/mirror_session.c`
- `scripts/test_airplay_mirror_runtime.c`
