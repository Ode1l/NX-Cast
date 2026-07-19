# Step 10: Phase 1 H.264 Hardware Mirror

> Status: BLOCKED
> Created: 2026-07-19

## Goal
完成 iPhone 屏幕到 Switch nvtegra/deko3d 视频显示的第一阶段端到端闭环。

## Prerequisites
- Step 9 completed — `airplay://mirror` produces a valid live H.264 MPEG-TS stream.
- A Switch/iPhone on the same Wi-Fi and `TRACE_AIRPLAY=1 TRACE_MEDIA=1` test build are available.

## Deliverables
- RECORD 成功后由 AirPlay 会话线程安全地请求播放器加载内部流，首个有效 IDR 后自动播放。
- 方向/分辨率变化、暂停、断连、重连和关键帧等待有明确 UI 状态与恢复策略。

## Plan
- [x] `read` `source/player/core/session.c`, `source/player/player.h`, and overlay loading-state paths — identify thread-safe command boundary.
- [x] `write` `source/protocol/airplay/media/mirror_runtime.[ch]` — translate mirror lifecycle events into queued player commands, never direct UI calls.
- [x] `edit` mirror/video and stream bridge modules — require an IDR when SPS/PPS generation changes and retain monotonic timestamps.
- [x] `edit` media runtime status mapping — expose Preparing, Waiting for keyframe, Playing, Disconnected and Error states.
- [ ] `bash` strict Switch build and nxlink deploy — strict traced build passes; real-device deploy/matrix is blocked by unavailable iPhone-compatible FairPlay key unwrap.
- [ ] `read` sanitized real-device traces — no real RECORD/media trace can exist until the Step 7 compatibility gate is resolved.

## Quality Checklist
- [x] Evidence-before-edit: player command/thread contract and UI state ownership recorded.
- [x] Existing pattern / reuse checked: IPTV/DLNA renderer commands and current loading overlay.
- [x] Contract understood: AirPlay owns player only during an active accepted session and releases it on all exits.
- [x] Risk reviewed: cross-thread UI/player calls, decoder reconfigure corruption, first-frame stall and stale sessions.
- [x] Mitigation recorded: fixed command queue, generation IDs, IDR gate and ten-cycle encrypted TCP test.

## Validation Checklist
- [x] Strict Switch build exits 0 with `TRACE_AIRPLAY=1 TRACE_MEDIA=1`; deploy remains unavailable in this environment.
- [ ] iPhone screen video renders through nvtegra/deko3d for 30 minutes without fatal error.
- [x] Ten simulated encrypted connect/RECORD/IDR/disconnect cycles release the media runtime cleanly.

## Test Checklist
- [ ] Real-device matrix covers portrait/landscape, lock/unlock, source app change, Wi-Fi interruption and reconnect; blocked by Step 7 FairPlay compatibility.

## Implementation Notes
- Added a dedicated AirPlay media worker. RTSP/mirror threads enqueue bounded generation-tagged load/play/stop commands and never call player/UI code directly.
- RECORD binds and loads `airplay://mirror`; autoplay is delayed until the first accepted IDR.
- Teardown first detaches/cancels the bounded bridge, joins mirror transport, then queues player stop/unbind.
- Config generation changes are rejected unless the first packet is a keyframe carrying refreshed SPS/PPS.
- Host validation performs ten real encrypted mirror TCP cycles with ASan/UBSan. Standard iPhone media acceptance remains honestly blocked by the Step 7 proprietary FairPlay boundary.

## Files Changed
- `makefile`
- `scripts/test_airplay_mirror_runtime.c`
- `scripts/test_airplay_stream_bridge.c`
- `source/protocol/airplay/media/mirror_runtime.c`
- `source/protocol/airplay/media/mirror_runtime.h`
- `source/protocol/airplay/media/stream_bridge.c`
- `source/protocol/airplay/media/stream_bridge.h`
