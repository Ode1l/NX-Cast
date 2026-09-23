# Step 3: Single-line scrolling titles

> Status: COMPLETED
> Created: 2026-09-22

## Goal
Replace title ellipsis with single-line scrolling and use a white progress fill.

## Prerequisites
- User explicitly chose scrolling rather than wrapping.
- Existing player layout and render functions inspected.

## Deliverables
- Stationary short titles, bounded marquee for overflow, pause at each end.
- UTF-8-safe title storage and rebuilt device package.

## Plan
- [x] Expand fixed title storage and preserve complete code points.
- [x] Replace ellipsis with render-thread-only scrolling; reset on title changes or hidden UI.
- [x] Run focused tests, build and package.

## Quality Checklist
- [x] Reuse existing UTF-8 helper; no new threads or protocol changes.
- [x] Clip only the title viewport; preserve layout and touch targets.

## Validation Checklist
- [x] Release build and package succeed.
- [x] Diff check passes.

## Test Checklist
- [x] Host UI tests and long-title UTF-8 checks pass.

## Implementation Notes
Release build completed without warnings; shared UI tests including 600-byte plain/XML titles and bounded UTF-8 overflow passed. Full SD ZIP rebuilt. Scroll uses monotonic time on the render thread, 32px/s with one-second end holds. UI hide/title replacement restarts the scroll. Visual motion and speed require Switch acceptance testing.

## Files Changed
`source/player/ui/overlay.h`, `source/player/ui/bar.c`, `source/player/render/imgui/imgui_overlay.cpp`, `scripts/test_player_title.c`, `scripts/test_ui.sh`, `docs/new-ui-device-test.md`, `CHANGELOG.md`.
