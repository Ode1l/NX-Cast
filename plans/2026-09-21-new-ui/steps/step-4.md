# Step 4: Device feedback

Status: COMPLETED

## Goal
Fix language persistence, remove IPTV playback timeline, allow outside-tap dismissal and simplify home icons.

## Prerequisites
Steps 1-3 implemented; user supplied device feedback.

## Deliverables
SD-safe preference replacement, consistent live controls, touch dismissal, matching vector icons.

## Plan
1. Correct persistence and preserve recoverable preferences on failed replacement.
2. Correct live presentation and input, add drawer dismissal.
3. Redraw icons, run host tests, build and package.

## Quality Checklist
- No protocol/decoder changes.
- Touch dismissal must not trigger playback underneath.
- Keep NX-Cast brand logo unchanged.

## Validation Checklist
- Host UI tests and Switch release build.
- Package rebuilt; physical appearance and SD persistence need device validation.

## Test Checklist
- Preference replacement/recovery and invalid destination.
- Drawer outside tap versus drag/modal.

## Implementation Notes
Live HLS can report a positive duration; it is not proof of a seekable VOD timeline.
IPTV local UI now hides the timeline and filters seeking on its snapshot copy;
protocol/backend capabilities and EPG row progress remain unchanged.
Preference replacement does not require overwrite-rename semantics. A backup
allows read recovery after interruption and rollback after failed installation.
The original device errno was not captured, so SD rename compatibility is a
targeted fix, not a confirmed diagnosis of the user's particular card.
Home feature icons use matching 4px strokes; the brand mark is unchanged.

Validation: `sh scripts/test_ui.sh` passed, including no-overwrite rename,
failed-install rollback, backup recovery, outside-tap and outside-drag tests.
`make RELEASE_JOBS=4 release-build` passed without warnings/errors.
`sh scripts/package_release.sh` passed its preset/layout/sensitive-file checks.
Physical SD persistence, touch ergonomics and icon appearance require user testing.

## Files Changed
- source/player/ui/home.c, browser.c
- source/main.c, source/player/render/imgui/imgui_overlay.cpp
- scripts/test_home_ui.c, test_iptv_browser.c, test_ui.sh
- CHANGELOG.md, docs/new-ui-device-test.md
