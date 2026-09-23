# Step 2: Complete IPTV browser and catalog

> Status: COMPLETED
> Created: 2026-09-21

## Goal
Finish the remaining approved IPTV navigation, bilingual UI and capacity work.
## Prerequisites
- Home slice completed; user explicitly requested all remaining work.
## Deliverables
- Approved 2026-09-22 player restyle: soft gradient, cyan timeline, smaller hints, transparent center controls; no protocol/decoder changes.
- Light full-screen browser, dark playback drawer, category/source selectors.
- Continuous controller/touch scrolling, explicit focus and active-channel indication.
- Dynamic catalog allocation and startup local playlist discovery.
- Bilingual controls and updated complete installation package.
## Plan
- [x] Restyle playback drawing and shared layout, retaining touch hit area; validate release build and package.
- [x] Implement data APIs and dynamic capacity with allocation failure handling.
- [x] Implement C browser state and shared input geometry.
- [x] Render both layouts and menus from the same state.
- [x] Translate built-in interface strings without altering stream metadata.
- [x] Build, run focused black-box tests and package device build.
## Quality Checklist
- [x] Existing IPTV mutex/worker/catalog and UI paths inspected.
- [x] Preserve playback ownership and prohibit input leakage.
- [x] Handle empty lists, missing guides, huge catalogs and cancelled gestures.
## Validation Checklist
- [x] Switch release build and complete ZIP succeed.
- [x] Diff check and focused host tests pass.
## Test Checklist
- [x] Catalog beyond previous limits.
- [x] Continuous scrolling, focus/modal navigation and touch activation.
- [x] Chinese/English persistence and UI prompts.
## Implementation Notes
Data, input and translation implementation are delegated with disjoint file ownership.
Rendering and integration are performed in the parent task.

Verified 2026-09-22: make test-ui and make test-iptv-data pass; browser/home/data sanitizer runs pass; release-build succeeds without compiler warnings; package_release.sh validates the complete ZIP, intact sources.txt and absence of runtime secrets. Fixed the pre-existing void-expression return in renderer_video_detach for strict C host compilation. No protocol/decoder implementation changed. Real Switch appearance, touch feel and stream regression testing remain user acceptance work; see docs/new-ui-device-test.md.
## Files Changed
`source/iptv/{iptv.c,iptv.h,data.h}`, `source/main.c`, `source/player/{view.h,renderer.h}`, `source/player/ui/{browser.c,browser.h,channel_list.h,home.c,home.h,layout.c,bar.c,controls.c,ui.c}`, `source/player/render/imgui/imgui_overlay.cpp`, `scripts/{test_ui.sh,test_home_ui.c,test_iptv_browser.c,test_iptv_data.c,test_player_layout.c,package_release.sh}`, `scripts/iptv_test_stubs/`, removed `scripts/test_iptv_channel_list.c`, `makefile`, README, CHANGELOG, `docs/{iptv.md,new-ui-device-test.md}`, IPTV asset README and GitHub workflows/release notes.
