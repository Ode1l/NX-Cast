# Step 12: Clock, Jitter, and A/V Synchronization

> Status: BLOCKED
> Created: 2026-07-19

## Goal
统一 AirPlay 时钟域并控制抖动和漂移，使镜像音画长期同步且断点可恢复。

## Prerequisites
- Step 11 completed — video/audio packets carry source timestamps and play together.
- Real-device traces include source timing, local monotonic time, queue depth and observed A/V offset.

## Deliverables
- `source/protocol/airplay/mirror/clock.[ch]` 维护 NTP/源时钟到本地媒体时钟映射和不连续检测。
- 音视频队列具有明确延迟目标、丢弃/静音/重同步策略和可脱敏观测指标。

## Plan
- [x] `rg` UxPlay/RPiPlay timing, NTP and jitter logic — compared RTP sync packets, clock units, filters and legacy differences.
- [x] `write` `source/protocol/airplay/mirror/clock.[ch]` — implemented wrap-safe conversion, bounded offset correction and discontinuity events.
- [x] `edit` audio/video modules and stream bridge — map timestamps once and enforce bounded skew/latency policy.
- [x] `edit` `TRACE_AIRPLAY` logs — added periodic queue/skew/drift counters without packet payloads or secrets.
- [x] `edit` host tests — simulate jitter, loss, clock wrap, jump and slow drift with deterministic timelines.
- [ ] `bash` host tests and 30-minute Switch soak — host/sanitizer/strict builds pass; real soak is blocked by Step 7 FairPlay compatibility.

## Quality Checklist
- [x] Evidence-before-edit: reference timing equations and observable packet units documented; real trace remains unavailable behind Step 7.
- [x] Existing pattern / reuse checked: local monotonic clock and libmpv timestamp handling.
- [x] Contract understood: one authoritative mapping per session; discontinuity starts a new clock generation.
- [x] Risk reviewed: integer wrap, feedback oscillation, growing latency, audible correction and false drift.
- [x] Mitigation recorded: deterministic simulation, bounded correction, hysteresis and soak acceptance limits.

## Validation Checklist
- [x] `make test-airplay` timing simulations exit 0, including ASan/UBSan.
- [ ] Strict Switch build links successfully; 30-minute real-device drift/queue acceptance is blocked by Step 7.
- [x] Deterministic disconnect/reconnect and source pause/resume simulations contain no old-clock state.

## Test Checklist
- [x] Simulations cover jitter, loss/retransmit, RTP/NTP wrap, clock jump, slow drift, pause/resume and restart.

## Implementation Notes
- Audio control packet `0x54` establishes the source RTP-to-NTP anchor; resent `0x56` packets are fed back through the same bounded packet parser.
- Video NTP and audio RTP are mapped exactly once to a shared 90 kHz session timeline before FFmpeg muxing.
- Offset correction uses a 1 ms deadband, 5 ms maximum step and 250 ms discontinuity threshold; audio more than 2 seconds from the latest video is dropped.
- RFC-style arrival variation is tracked as an EWMA. Periodic trace output contains generation, skew, jitter, drift and drop counters only.
- A video timestamp discontinuity starts a new clock generation, clears the audio anchor and waits for a fresh sync packet.

## Files Changed
- `makefile`
- `scripts/test_airplay_audio.c`
- `scripts/test_airplay_clock.c`
- `source/protocol/airplay/media/mirror_runtime.c`
- `source/protocol/airplay/media/stream_bridge.[ch]`
- `source/protocol/airplay/mirror/audio.[ch]`
- `source/protocol/airplay/mirror/clock.[ch]`
