# Step 1: Implement bilingual home UI

> Status: COMPLETED
> Created: 2026-09-21

## Goal
Provide the latest approved passive-cast home and a compiled test artifact.
## Prerequisites
- Approved design and clean new-ui branch.
## Deliverables
- Home language selection and TV entry integrated with existing input.
## Plan
- [x] Add C language preferences and home geometry.
- [x] Replace home rendering; align input hit areas.
- [x] Compile and run focused navigation/settings checks.
## Quality Checklist
- [x] Renderer, input, fonts and storage patterns inspected.
- [x] Preserve media ownership and worker lifetimes.
- [x] Shared geometry and text clipping inspected; hardware visual check remains in device checklist.
## Validation Checklist
- [x] Release build succeeds.
- [x] Diff whitespace check succeeds.
## Test Checklist
- [x] Navigation and language preferences pass host checks.
- [x] Device checklist supplied.
## Implementation Notes
Implemented home only after the subsequent design discussion. Full-screen IPTV,
playback drawer, catalog capacity and other screen translations remain pending.
Physical Switch validation is documented in docs/new-ui-device-test.md.
## Files Changed
source/main.c; source/player/view.h; source/player/ui/home.c;
source/player/ui/home.h; source/player/render/imgui/imgui_overlay.cpp;
scripts/test_home_ui.c; docs/new-ui-device-test.md.
