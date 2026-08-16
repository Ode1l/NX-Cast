# Step 1: Repair the iOS RTSP Control Transcript

> Status: COMPLETED
> Created: 2026-08-14

## Goal
Keep current iOS AirPlay sessions alive through `/audioMode` and empty parameter updates so media SETUP can continue.

## Prerequisites
- Latest trace has been classified as a pre-media negotiation failure.
- Files to modify: `source/protocol/airplay/protocol/handlers.c`, `scripts/test_airplay_handlers.c`.
- Design: tolerant no-op responses are limited to UxPlay-compatible requests and do not introduce playback side effects.

## Deliverables
- `POST /audioMode` returns 200 for a valid binary-plist request.
- Empty `SET_PARAMETER` returns 200 in setup/recording state while unsupported non-empty content types remain 400.
- After this step: the focused handler transcript and complete `make test-airplay` suite pass.

## Plan
- [x] `read` `source/protocol/airplay/protocol/handlers.c` and `scripts/test_airplay_handlers.c` — confirm route order, state guards, and test helper conventions.
- [x] `edit` `scripts/test_airplay_handlers.c` — add protective transcript assertions for `/audioMode`, empty `SET_PARAMETER`, and unsupported non-empty input.
- [x] `edit` `source/protocol/airplay/protocol/handlers.c` — add bounded plist validation/logging for `/audioMode` and accept empty parameter no-ops.
- [x] `bash` `make test-airplay` — expect all AirPlay and shared regression tests to pass.

## Quality Checklist
- [x] Evidence-before-edit: target read `handlers.c`, impact search `rg -n "audioMode|SET_PARAMETER" source scripts`, validation `make test-airplay`.
- [x] Existing pattern / reuse checked: reuse `content_type_is`, plist helpers, and current route/state guards.
- [x] Contract understood: inputs are established RTSP requests; outputs are status-only responses; no media mutation.
- [x] Risk reviewed: compatibility and parser input validation.
- [x] Mitigation recorded: bounded plist decode plus positive and negative transcript tests.

## Validation Checklist
- [x] `make test-airplay` exits 0.
- [x] `git diff --check` exits 0 for changed files.

## Test Checklist
- [x] Handler test covers 200 for valid `/audioMode`.
- [x] Handler test covers 200 for empty `SET_PARAMETER` and 400 for unsupported non-empty content.

## Implementation Notes
The new transcript failed first with the expected 501 and 400 responses. `handle_audio_mode()` now validates the binary plist and acknowledges any non-empty mode without changing state. Empty parameter updates are treated as no-ops; existing non-empty content-type validation remains intact. The full `make test-airplay` suite then passed.

## Files Changed
- `source/protocol/airplay/protocol/handlers.c`
- `scripts/test_airplay_handlers.c`
