# Step 9: Live Media Stream Bridge

> Status: PENDING
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
- [ ] `read` `source/player/backend/libmpv.c` and installed `mpv/stream_cb.h` — lock callback lifetime/thread contract.
- [ ] `rg` current FFmpeg use in `source` and pkg-config output — reuse installed libraries without new media dependency.
- [ ] `write` `source/protocol/airplay/media/stream_bridge.[ch]` — mux H.264 to MPEG-TS in a bounded producer/consumer buffer.
- [ ] `edit` `source/player/backend/libmpv.c` — register custom scheme and close/cancel callbacks before mpv initialization.
- [ ] `edit` `scripts/test_airplay.c` — feed fixture access units, drain stream, test wrap/backpressure/cancel and save TS artifact.
- [ ] `bash` `make test-airplay`, `ffprobe` TS artifact and strict Switch build — expect one valid H.264 stream.

## Quality Checklist
- [ ] Evidence-before-edit: libmpv callback and FFmpeg API availability recorded from installed headers/libs.
- [ ] Existing pattern / reuse checked: existing libmpv initialization and player session ownership.
- [ ] Contract understood: stream is non-seekable; producer never blocks shutdown indefinitely; consumer owns EOF.
- [ ] Risk reviewed: deadlock, unbounded latency, FFmpeg callback reentrancy, timestamp discontinuity and buffer lifetime.
- [ ] Mitigation recorded: fixed capacity, condition-variable cancellation, monotonic PTS and stress tests.

## Validation Checklist
- [ ] `make test-airplay` exits 0.
- [ ] `ffprobe -v error -show_streams build/tests/airplay-mirror.ts` reports H.264 video.
- [ ] Strict Switch build exits 0 with libmpv/deko3d required.

## Test Checklist
- [ ] Bridge tests cover steady flow, wraparound, slow consumer, cancel, EOF and restart.

## Implementation Notes
Pending.

## Files Changed
Pending.
