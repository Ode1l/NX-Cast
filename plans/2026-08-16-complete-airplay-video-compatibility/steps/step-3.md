# Step 3: Audio-First Playback

> Status: COMPLETED
> Created: 2026-08-16

## Goal
Make associated audio and audio-only-arrival sessions playable while preserving safe promotion to a later type-110 video stream.

## Prerequisites
- Step 2 completed — remote media transport and session ownership are stable.
- Files to modify: `media/stream_bridge.*`, `mirror_runtime.*`, `handlers.*`, integration callbacks, and bridge/runtime tests.
- Existing FFmpeg/mpv/deko3d backend remains unchanged.

## Deliverables
- Stream bridge creation has an explicit media profile instead of assuming H.264 is always present.
- RECORD can start an audio-first generation; type-110 arriving before header emission joins it, while later arrival rotates to a combined generation safely.
- Network callbacks enqueue lifecycle commands and never call the player directly.

## Plan
- [x] `edit` `stream_bridge.*` — support audio-only and combined profiles with immutable emitted headers.
- [x] `edit` `mirror_runtime.*` — model audio-first start and bounded generation replacement for late video.
- [x] `edit` handlers/integration callbacks — propagate audio readiness and RECORD state through the existing actor boundary.
- [x] `write` host tests — audio-only, audio-before-video, late-video rotation, stop, and teardown.
- [x] `bash` `make test-airplay` — expect 0 failures.

## Quality Checklist
- [ ] Evidence-before-edit: target read `stream_bridge.c`, `mirror_runtime.c`, `handlers.c`; impact search `rg "push_audio|record_callback|bridge_create"`
- [ ] Existing pattern / reuse checked: mirror runtime command queue and bridge generation counters
- [ ] Contract understood: AVFormatContext is bridge-thread owned; headers are immutable once written; player calls are actor-owned
- [ ] Risk reviewed: concurrency, FFmpeg lifecycle, memory ownership, behavior regression
- [ ] Mitigation recorded: generation fencing, explicit profiles, no cross-thread AVFormat mutation, lifecycle tests

## Validation Checklist
- [x] `git diff --check` exits 0
- [x] Bridge/runtime host targets compile with warnings as errors

## Test Checklist
- [x] Audio-first lifecycle tests pass
- [x] `make test-airplay` passes

## Implementation Notes
- Bridge profiles are immutable after construction; a Matroska header is never mutated to add a late video stream.
- Audio-only mode establishes its timeline from the first audio sync packet. Combined A/V mode retains the existing video-master clock behavior.
- Late video replacement is one runtime-worker command: stop and unbind the previous generation, rekey ownership, then bind and load the combined bridge.

## Files Changed
- `source/protocol/airplay/media/stream_bridge.c`
- `source/protocol/airplay/media/stream_bridge.h`
- `source/protocol/airplay/media/mirror_runtime.c`
- `source/protocol/airplay/media/mirror_runtime.h`
- `source/protocol/airplay/mirror/clock.c`
- `source/protocol/airplay/mirror/clock.h`
- `source/protocol/airplay/protocol/handlers.c`
- `source/protocol/airplay/protocol/handlers.h`
- `source/protocol/airplay/integration.c`
- `source/protocol/airplay/receiver.c`
- `source/protocol/airplay/receiver.h`
- `scripts/test_airplay_audio.c`
- `scripts/test_airplay_handlers.c`
- `scripts/test_airplay_mirror_runtime.c`
