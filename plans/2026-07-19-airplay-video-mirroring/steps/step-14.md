# Step 14: Ownership, UI, and Shutdown Integration

> Status: BLOCKED
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
- [x] `read` startup/shutdown and player/UI ownership paths in `source/main.c` and `source/player` — map all callers and threads.
- [x] `rg` `renderer_set_uri`, stop, home and channel actions — inventory every protocol/player ownership transition.
- [x] `edit` `source/player/core/session.c` and public headers — add minimal owner/generation arbitration without duplicating player state.
- [x] `edit` AirPlay composition, DLNA/IPTV callers and ImGui overlay — show PIN/loading/error and consistent return-home behavior.
- [x] `edit` `source/main.c` — start AirPlay after network/player readiness and stop it before player/UI teardown.
- [ ] `bash` mixed-protocol real-device matrix — host tests and strict build pass; hardware transitions and + exit remain unavailable locally.

## Quality Checklist
- [x] Evidence-before-edit: all player callers and shutdown dependencies listed via `rg`/source read.
- [x] Existing pattern / reuse checked: current session mutex/event queue and ImGui error/status surfaces.
- [x] Contract understood: one owner/generation controls media and stale leases cannot issue player commands.
- [x] Risk reviewed: deadlock, use-after-free, protocol starvation, stale UI and network-after-player teardown.
- [x] Mitigation recorded: short ownership locks, generation checks, idempotent stop and network-before-player teardown.

## Validation Checklist
- [x] `make test-airplay` and existing IPTV channel navigation test exit 0.
- [x] Strict Switch build exits 0.
- [ ] DLNA -> AirPlay -> IPTV transitions and + exit complete without crash or hung thread.

## Test Checklist
- [ ] Real-device matrix covers competing requests, return home, app exit during pair/load/play and network loss.

## Implementation Notes
- Added a generation-bearing `PlayerOwnershipLease` shared by DLNA, IPTV, AirPlay remote video and AirPlay mirroring. A new claim invalidates all older leases, so stale network callbacks fail closed.
- Added the Switch-only AirPlay integration root after network/player readiness. It composes receiver, remote video, mirror runtime and libmpv bridge without exposing PINs or keys to logs.
- AirPlay mirroring remains unadvertised because no audited FairPlay unwrap callback exists. URL/HLS startup also fails closed when the required Ed25519 backend is unavailable.
- Shutdown now stops AirPlay and DLNA listeners/workers before the player, then deinitializes IPTV/UI/player before logs and network. This also removes the old branch that skipped `player_deinit()` whenever DLNA had run.
- Host ownership tests cover concurrent claims and stale release/command rejection. Real mixed-protocol and shutdown acceptance remains blocked until Switch/iPhone hardware is available.

## Files Changed
- `source/player/core/ownership.[ch]`, `source/player/core/session.c`, `source/player/player.h`
- `source/protocol/airplay/integration.[ch]`, `source/protocol/airplay/media/remote_video.[ch]`
- `source/protocol/dlna/control/action/avtransport.c`, `source/protocol/dlna/control/action/renderingcontrol.c`
- `source/iptv/iptv.c`, `source/main.c`, `source/player/view.h`, `source/player/render/imgui/imgui_overlay.cpp`
- `scripts/test_player_ownership.c`, `scripts/test_airplay_remote_video.c`, `makefile`
