# Step 14: Ownership, UI, and Shutdown Integration

> Status: PENDING
> Created: 2026-07-19

## Goal
把 AirPlay 纳入 NX-Cast 单一播放器所有权、主页/UI 状态和可靠退出顺序，避免与 DLNA/IPTV 冲突。

## Prerequisites
- Step 13 completed — mirror and remote-video sessions both use existing player APIs.
- Files to inspect: `source/main.c`, `source/player/core/session.c`, IPTV home/overlay code and DLNA stop order.

## Deliverables
- 明确的媒体所有者状态在 AirPlay/DLNA/IPTV 之间仲裁连接、切换、返回主页和错误恢复。
- 退出顺序先停止 AirPlay 广播/连接/媒体线程，再释放 player/UI，最后按现有策略退出日志和网络。

## Plan
- [ ] `read` startup/shutdown and player/UI ownership paths in `source/main.c` and `source/player` — map all callers and threads.
- [ ] `rg` `renderer_set_uri`, stop, home and channel actions — inventory every protocol/player ownership transition.
- [ ] `edit` `source/player/core/session.c` and public headers — add minimal owner/generation arbitration without duplicating player state.
- [ ] `edit` `source/protocol/airplay/airplay.c`, DLNA/IPTV callers and ImGui overlay — show PIN/loading/error and consistent return-home behavior.
- [ ] `edit` `source/main.c` — start AirPlay after network/player readiness and stop it before player/UI teardown.
- [ ] `bash` host tests, strict build and mixed-protocol real-device matrix — verify no stale command or shutdown crash.

## Quality Checklist
- [ ] Evidence-before-edit: all player callers and shutdown dependencies listed via `rg`/source read.
- [ ] Existing pattern / reuse checked: current session mutex/event queue and ImGui error/status surfaces.
- [ ] Contract understood: one owner/generation controls media; network threads post events only.
- [ ] Risk reviewed: deadlock, use-after-free, protocol starvation, stale UI and network-after-player teardown.
- [ ] Mitigation recorded: lock ordering, generation checks, idempotent stop and repeated shutdown tests.

## Validation Checklist
- [ ] `make test-airplay` and existing host tests exit 0.
- [ ] Strict Switch build exits 0.
- [ ] DLNA -> AirPlay -> IPTV transitions and + exit complete without crash or hung thread.

## Test Checklist
- [ ] Real-device matrix covers competing requests, return home, app exit during pair/load/play and network loss.

## Implementation Notes
Pending.

## Files Changed
Pending.
