# Step 2: Replace Cast icon

> Status: COMPLETED
> Created: 2026-09-23

## Goal
Replace the awkward four-edge outline with a familiar solid Cast silhouette.
## Prerequisites
- Step 1 completed; user reports the Cast icon is still visually wrong.
- Remix Icon Cast source inspected; MIT license.
## Deliverables
- Archived upstream SVG/license and matching home drawing.
## Plan
- [x] Add the upstream Cast SVG and MIT attribution.
- [x] Replace custom outlined screen drawing with filled silhouette and broadcast mark.
- [x] Run build and package validation.
## Quality Checklist
- [x] Inspect upstream asset and rendering support.
- [x] Keep scope to home Cast icon and attribution.
- [x] Verify release build and package.
## Validation Checklist
- [x] Switch release build succeeds.
- [x] Package contains third party notice and license.
## Test Checklist
- [ ] UI host suite passes; inspect source geometry.
- Device screenshot requested from user after upload.
## Implementation Notes
Added Remix Icon's `cast-fill.svg` and MIT license. Home drawing now uses
disconnected solid screen bars, leaving the lower-left open for three broadcast
arcs. Host UI tests passed; release build produced NX-Cast.nro without warnings
or errors; package validation passed. Device rendering still requires user review.
## Files Changed
- assets/icon/remix-cast-fill.svg
- assets/licenses/LICENSE.RemixIcon.MIT.txt
- source/player/render/imgui/imgui_overlay.cpp
- third_party/NOTICE.md
