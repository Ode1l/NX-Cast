# Step 10: Phase 1 H.264 Hardware Mirror

> Status: PENDING
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
- [ ] `read` `source/player/core/session.c`, `source/player/player.h`, and overlay loading-state paths — identify thread-safe command boundary.
- [ ] `edit` `source/protocol/airplay/airplay.c` — translate mirror lifecycle events into queued player commands, never direct UI calls.
- [ ] `edit` mirror/video and stream bridge modules — flush/reconfigure safely on SPS/PPS or timestamp discontinuity.
- [ ] `edit` player status/error mapping — expose Preparing, Waiting for keyframe, Playing and Disconnected states.
- [ ] `bash` strict Switch build and nxlink deploy — run rotation, lock/unlock, app switch, packet-loss and 10 reconnect tests.
- [ ] `read` sanitized traces — confirm nvtegra is selected, queues remain bounded and teardown completes.

## Quality Checklist
- [ ] Evidence-before-edit: player command/thread contract and UI state ownership recorded.
- [ ] Existing pattern / reuse checked: IPTV/DLNA renderer commands and current loading overlay.
- [ ] Contract understood: AirPlay owns player only during an active accepted session and releases it on all exits.
- [ ] Risk reviewed: cross-thread UI/player calls, decoder reconfigure corruption, first-frame stall and stale sessions.
- [ ] Mitigation recorded: command queue, generation IDs, IDR gate, flush tests and reconnect matrix.

## Validation Checklist
- [ ] Strict Switch build exits 0 and deploys.
- [ ] iPhone screen video renders through nvtegra/deko3d for 30 minutes without fatal error.
- [ ] Ten connect/disconnect cycles return to the home screen cleanly.

## Test Checklist
- [ ] Real-device matrix covers portrait/landscape, lock/unlock, source app change, Wi-Fi interruption and reconnect.

## Implementation Notes
Pending.

## Files Changed
Pending.
