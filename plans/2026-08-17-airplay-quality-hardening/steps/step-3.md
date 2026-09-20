# Step 3: Propagate Audio Record Failures

> Status: COMPLETED
> Created: 2026-08-17

## Goal
Make audio-only record operations return success/failure so the RTSP response and integration status reflect actual runtime state.

## Prerequisites
- Step 2 completed.
- Files to modify: `source/protocol/airplay/protocol/handlers.h`, `source/protocol/airplay/protocol/handlers.c`, `source/protocol/airplay/integration.c`, `source/protocol/airplay/media/mirror_runtime.h`, `source/protocol/airplay/media/mirror_runtime.c`, `scripts/test_airplay_handlers.c`, `scripts/test_airplay_mirror_runtime.c`.

## Deliverables
- Audio record callback and runtime API return `bool`.
- Handler returns an error when audio-only record fails.
- Integration sets an error status on callback failure.
- Focused tests cover success and no-op failure.

## Plan
- [x] `edit` source/protocol/airplay/protocol/handlers.h — change `AirPlayAudioRecordCallback` to return `bool`.
- [x] `edit` source/protocol/airplay/media/mirror_runtime.h and mirror_runtime.c — make `airplay_mirror_runtime_record_audio` return `bool`.
- [x] `edit` source/protocol/airplay/integration.c — return runtime result and set success/failure status.
- [x] `edit` source/protocol/airplay/protocol/handlers.c — fail setup/record when the audio callback returns false.
- [x] `edit` scripts/test_airplay_handlers.c and scripts/test_airplay_mirror_runtime.c — update callbacks and add failure/no-op checks.
- [x] `bash` make test-airplay — expect exit 0.

## Quality Checklist
- [x] Evidence-before-edit: callback and integration paths re-read; impact search `rg audio_record_callback|airplay_mirror_runtime_record_audio`; validation `make test-airplay`.
- [x] Existing pattern / reuse checked: existing boolean runtime APIs provide the error-return convention.
- [x] Contract understood: success is not recorded when runtime no-ops.
- [x] Risk reviewed: correctness / API / observability.
- [x] Mitigation recorded: focused success/failure tests and error logging.

## Validation Checklist
- [x] `git diff --check` exits 0

## Test Checklist
- [x] `make test-airplay` — all pass

## Implementation Notes
Converted audio record callback and runtime API to boolean. Handler setup and standalone RECORD now return 461 when the callback or runtime state fails. Integration status reflects failure instead of claiming that audio is waiting.

## Files Changed
- `source/protocol/airplay/protocol/handlers.h`
- `source/protocol/airplay/protocol/handlers.c`
- `source/protocol/airplay/integration.c`
- `source/protocol/airplay/media/mirror_runtime.h`
- `source/protocol/airplay/media/mirror_runtime.c`
- `scripts/test_airplay_handlers.c`
- `scripts/test_airplay_mirror_runtime.c`
