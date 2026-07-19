# Step 1: AirPlay Lifecycle and Test Harness

> Status: COMPLETED
> Created: 2026-07-19

## Goal
建立不广播网络能力的 AirPlay 模块生命周期、状态契约和可重复执行的主机测试入口。

## Prerequisites
- 用户批准 `plan.md` 中的 clean-room、独立控制服务和媒体桥接设计。
- Files to inspect: `source/main.c`, `source/protocol/airplay/discovery/mdns.c`, `makefile`, `scripts/test_iptv_channel_list.c`.

## Deliverables
- `source/protocol/airplay/airplay.h` 提供 start/stop/status 和事件回调边界，`airplay.c` 保持默认禁用且可重复启停。
- `make test-airplay` 能运行 AirPlay 主机测试，且严格 Switch 构建通过。

## Plan
- [x] `read` `source/main.c` and `source/protocol/airplay/discovery/mdns.c` — lock current startup/shutdown behavior before edits.
- [x] `rg` `source Makefile scripts` for lifecycle/test patterns — reuse existing logging, mutex and host-test conventions.
- [x] `write` `source/protocol/airplay/airplay.h` and `source/protocol/airplay/airplay.c` — add idempotent lifecycle facade and bounded configuration/state types.
- [x] `write` `scripts/test_airplay.c` — cover invalid config, double start, double stop and callback isolation.
- [x] `edit` `makefile` — compile the module and add a `test-airplay` host target without enabling discovery.
- [x] `bash` `make test-airplay` and strict Switch build — expect zero warnings/errors.

## Quality Checklist
- [x] Evidence-before-edit: target read `source/main.c`; impact search `rg -n airplay source Makefile`; validation `make test-airplay`.
- [x] Existing pattern / reuse checked: `source/protocol/dlna_control.c`, `scripts/test_iptv_channel_list.c`.
- [x] Contract understood: lifecycle is idempotent; no network thread or player side effect in this step.
- [x] Risk reviewed: correctness / API / project-fit; shutdown ordering, global state and host/Switch portability.
- [x] Mitigation recorded: bounded config copy, state-transition tests and discovery remains disabled.

## Validation Checklist
- [x] `make test-airplay` exits 0.
- [x] `make clean && make NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 -j4` exits 0.

## Test Checklist
- [x] `scripts/test_airplay.c` covers lifecycle success and failure paths; ASan/UBSan run also exits 0.

## Implementation Notes
Added a dependency-free C lifecycle facade with bounded friendly-name storage, status snapshots, optional state callbacks and idempotent start/stop behavior. The API is explicitly application-thread-only until later transport steps add synchronization. No `main.c` call or discovery behavior was enabled, so existing runtime networking is unchanged.

## Files Changed
- `makefile`
- `source/protocol/airplay/airplay.h`
- `source/protocol/airplay/airplay.c`
- `scripts/test_airplay.c`
- `plans/2026-07-19-airplay-video-mirroring/plan.md`
- `plans/2026-07-19-airplay-video-mirroring/steps/step-1.md`
