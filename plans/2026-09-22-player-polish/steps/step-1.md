# Step 1: Menu transition and consistent drawing

> Status: COMPLETED
> Created: 2026-09-22

## Goal
Close the browser on successful playback and unify home/video hints without fallback cleanup on hide.
## Prerequisites
- Current UI and frontend paths inspected; user approved Switch-style direction.
## Deliverables
- Updated selection and ImGui behavior, compiled test bundle.
## Plan
- [x] Edit selection success branch to close the browser.
- [x] Share Switch key glyphs and refine cast icon.
- [x] Treat hidden UI as handled; test, compile and package.
## Quality Checklist
- [x] Read targets and fallback caller before editing.
- [x] Reuse existing hint layout and touch regions.
- [x] Preserve error fallback and failed-play menu.
## Validation Checklist
- [x] Release build and diff whitespace check.
- [x] Package verification.
## Test Checklist
- [x] Existing UI tests pass.
- Device: selection hides drawer; tap-hide removes whole UI together while playing/paused/loading.
## Implementation Notes
Empty ImGui output previously returned failure and invoked legacy black rectangle
cleanup after the video frame had rendered. It now returns handled without
submitting UI geometry. This fixes a verified fallback defect; the observed
text/geometry timing cannot be fully attributed or validated without hardware.
Selection uses existing browser reset after accepted play; failed requests retain
the menu. Shared key drawing preserves existing player touch-zone positions.
## Files Changed
- source/main.c
- source/player/render/imgui/imgui_overlay.cpp
- docs/new-ui-device-test.md
