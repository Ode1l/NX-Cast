# Step 3: Separate Audio-Only From Mirror Loading

> Status: COMPLETED
> Created: 2026-08-16

## Goal
Keep audio-only RTP ingress active without claiming mirror ownership or loading `airplay://mirror`; type-110 promotion remains the trigger for player handoff.

## Prerequisites
- Step 2 completed.
- Files to modify: `source/protocol/airplay/protocol/handlers.h`, `source/protocol/airplay/protocol/handlers.c`, `source/protocol/airplay/integration.c`, `source/protocol/airplay/media/mirror_runtime.h`, `source/protocol/airplay/media/mirror_runtime.c`, `scripts/test_airplay_handlers.c`, `scripts/test_airplay_mirror_runtime.c`.

## Deliverables
- Audio-only `RECORD` calls an audio-only callback and does not claim `airplay-mirror`.
- `airplay_mirror_runtime_record_audio` enables ingress while leaving player state unowned.
- Existing type-110 promotion still calls mirror record and performs one bridge promotion/player load.
- Focused tests cover audio-only and late-video transitions.

## Plan
- [x] `edit` source/protocol/airplay/protocol/handlers.h — add `audio_record_callback` to the handler config.
- [x] `edit` source/protocol/airplay/protocol/handlers.c — call media record only for mirror setup and audio record for audio-only setup.
- [x] `edit` source/protocol/airplay/media/mirror_runtime.h — add `airplay_mirror_runtime_record_audio`.
- [x] `edit` source/protocol/airplay/media/mirror_runtime.c — implement audio-only recording without enqueueing LOAD; require `runtime->mirror` for the existing record/load path.
- [x] `edit` source/protocol/airplay/integration.c — install `integration_audio_record` and set `Waiting for AirPlay video`.
- [x] `edit` scripts/test_airplay_handlers.c and scripts/test_airplay_mirror_runtime.c — pin callback selection and no-owner/no-load audio-only behavior.
- [x] `bash` make test-airplay — expect exit 0.

## Quality Checklist
- [x] Evidence-before-edit: handler/runtime/integration callbacks re-read; impact search `rg media_record_callback`; validation `make test-airplay`.
- [x] Existing pattern / reuse checked: existing `AIRPLAY_RUNTIME_COMMAND_REPLACE` is reused for promotion.
- [x] Contract understood: audio-only is ingress only; mirror ownership is type-110 gated.
- [x] Risk reviewed: correctness / API / project-fit.
- [x] Mitigation recorded: focused lifecycle tests plus full AirPlay suite.

## Validation Checklist
- [x] `git diff --check` exits 0

## Test Checklist
- [x] `make test-airplay` — all pass

## Implementation Notes
Added a separate audio-record callback and runtime API. Audio-only ingress no longer triggers player load, and type-110 promotion now completes with the normal mirror record/LOAD path. Added `load_queued` so player-load idempotency is separate from the existing keyframe-driven play flag.

## Files Changed
- `source/protocol/airplay/protocol/handlers.h`
- `source/protocol/airplay/protocol/handlers.c`
- `source/protocol/airplay/receiver.h`
- `source/protocol/airplay/receiver.c`
- `source/protocol/airplay/media/mirror_runtime.h`
- `source/protocol/airplay/media/mirror_runtime.c`
- `source/protocol/airplay/integration.c`
- `scripts/test_airplay_handlers.c`
- `scripts/test_airplay_mirror_runtime.c`
