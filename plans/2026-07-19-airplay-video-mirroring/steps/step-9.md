# Step 9: Live Media Stream Bridge

> Status: COMPLETED
> Created: 2026-07-19

## Goal
把实时 H.264 访问单元封装为非 seekable MPEG-TS，并通过 libmpv 自定义流送入现有播放器后端。

## Prerequisites
- Step 8 completed — mirror video callback emits valid timestamped H.264 access units.
- Installed libmpv `stream_cb.h` and FFmpeg avformat/avio APIs have been verified.

## Deliverables
- `source/protocol/airplay/media/stream_bridge.[ch]` 提供有界环形缓冲、FFmpeg mux、EOF/cancel/backpressure 语义。
- `source/player/backend/libmpv.c` 注册内部 `airplay://mirror` 只读流且保持 nvtegra/deko3d 选项不变。

## Plan
- [x] `read` `source/player/backend/libmpv.c` and installed `mpv/stream_cb.h` — lock callback lifetime/thread contract.
- [x] `rg` current FFmpeg use in `source` and pkg-config output — reuse installed libraries without new media dependency.
- [x] `write` `source/protocol/airplay/media/stream_bridge.[ch]` — mux H.264 to MPEG-TS in a bounded producer/consumer buffer.
- [x] `edit` `source/player/backend/libmpv.c` — register custom scheme and close/cancel callbacks before mpv initialization.
- [x] `edit` `scripts/test_airplay_stream_bridge.c` — feed fixture access units, drain stream, test wrap/backpressure/cancel and save TS artifact.
- [x] `bash` `make test-airplay`, `ffprobe` TS artifact and strict Switch build — expect one valid H.264 stream.

## Quality Checklist
- [x] Evidence-before-edit: libmpv callback and FFmpeg API availability recorded from installed headers/libs.
- [x] Existing pattern / reuse checked: existing libmpv initialization and player session ownership.
- [x] Contract understood: stream is non-seekable; producer never blocks shutdown indefinitely; consumer owns EOF.
- [x] Risk reviewed: deadlock, unbounded latency, FFmpeg callback reentrancy, timestamp discontinuity and buffer lifetime.
- [x] Mitigation recorded: fixed capacity, condition-variable cancellation, monotonic PTS and stress tests.

## Validation Checklist
- [x] `make test-airplay` exits 0.
- [x] `ffprobe -v error -show_streams build/tests/airplay-mirror.ts` reports H.264 video.
- [x] Strict Switch build exits 0 with libmpv/deko3d required.

## Test Checklist
- [x] Bridge tests cover steady flow, wraparound, slow consumer, cancel, EOF and restart.

## Implementation Notes
- Registered `airplay://mirror` before `mpv_initialize()` and kept every callback free of libmpv calls.
- The backend retains the selected bridge; open streams take an additional reference and enforce one active consumer.
- FFmpeg writes non-seekable MPEG-TS into a 2 MiB default bounded ring with cancel-aware condition-variable backpressure.
- Raw AirPlay NTP timestamps are converted to monotonic 90 kHz PTS; EOF and cancellation remain distinct.
- Host FFmpeg discovery follows the existing Homebrew-aware dependency pattern.

## Files Changed
- `makefile`
- `scripts/test_airplay_stream_bridge.c`
- `source/player/backend/libmpv.c`
- `source/player/backend/libmpv_airplay.h`
- `source/protocol/airplay/media/stream_bridge.c`
- `source/protocol/airplay/media/stream_bridge.h`
