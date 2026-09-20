# Step 2: Gate IPTV Controls by Media Owner

> Status: COMPLETED
> Created: 2026-09-01

## Goal
Expose IPTV channel navigation only when IPTV owns the active player lease.

## Prerequisites
- Step 1 completed with the AirPlay HLS path restored.
- Files to modify: `source/player/view.h`, `source/player/ui/channel_list.h`, `source/main.c`, `source/player/render/imgui/imgui_overlay.cpp`, `scripts/test_iptv_channel_list.c`.
- Design: the coordinator snapshot is the ownership source of truth.

## Deliverables
- `X`, stick, and touch channel entry are ignored during AirPlay and DLNA playback.
- An open IPTV drawer closes when another protocol takes ownership.
- Player hints omit `X Channels` outside IPTV playback.
- After this step: host tests and Switch development build pass.

## Plan
- [x] `edit` `source/player/ui/channel_list.h` and `scripts/test_iptv_channel_list.c` — define and protect the shared IPTV video-menu availability rule.
- [x] `edit` `source/player/view.h` and `source/main.c` — propagate active IPTV ownership and gate all channel menu entry paths.
- [x] `edit` `source/player/render/imgui/imgui_overlay.cpp` — render channel hints only for IPTV-owned playback using the shared rule.
- [x] `bash` standalone IPTV channel-list test and `make test-airplay` — verify navigation and protocol regressions.
- [x] `bash` `make dev-build BUILD_JOBS=4` — verify the C/C++ state contract and Switch linkage.

## Quality Checklist
- [x] Evidence-before-edit: target reads and `rg Channels|HidNpadButton_X` complete; validation commands identified.
- [x] Existing pattern / reuse checked: extended `PlayerHomeViewState`; added no second ownership store.
- [x] Contract understood: coordinator owner controls availability; channel library count controls only whether IPTV has a useful menu.
- [x] Risk reviewed: stale drawer state, touch hitboxes, home-screen IPTV access, C/C++ struct compatibility.
- [x] Mitigation recorded: close drawer on owner change, gate only video-player controls, full Switch build.

## Validation Checklist
- [x] Standalone IPTV channel-list test and `make test-airplay` exit 0.
- [x] `make dev-build BUILD_JOBS=4` exits 0.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] IPTV channel-list navigation host tests remain green.
- [x] AirPlay/coordinator host suite remains green.

## Implementation Notes
`PlayerHomeViewState` now carries the coordinator-derived IPTV ownership bit. A shared inline rule combines ownership and channel count for both C input handling and C++ hint rendering. The home-screen IPTV browser remains unchanged; only video-player channel controls are gated. Any drawer left open when AirPlay or DLNA takes over is closed before input dispatch.

## Files Changed
- `source/player/view.h`
- `source/player/ui/channel_list.h`
- `scripts/test_iptv_channel_list.c`
- `source/main.c`
- `source/player/render/imgui/imgui_overlay.cpp`
