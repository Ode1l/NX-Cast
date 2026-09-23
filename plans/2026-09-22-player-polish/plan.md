# Plan: Consistent player presentation

> Status: COMPLETED
> Created: 2026-09-22
> Last Updated: 2026-09-22

## Goal
Close channels after playback selection and unify Switch hints without a legacy-renderer hide flash.
## Assumptions
- Physical appearance and frame pacing require Switch testing.
## Open Questions
None.
## Spec-Lite
### Acceptance Criteria
- Successful selection closes the browser; failed selection retains it.
- Hidden ImGui UI is a successful no-op, not fallback rendering.
- Home/player share Switch key shapes; existing touch targets remain.
### Non-goals
- Protocol, decoder or playback-state redesign.
### Edge Cases
- Loading, paused video, failed playback request, narrow translated hints.
## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|---|---|---|---|
| Key hints | Generic pills / Switch glyphs | Shared Switch glyphs | User requested |
| Channel selection | Keep drawer / close | Close on success | User requested |
## Steps Overview
| Step | File | Status | Goal |
|---|---|---|---|
| 1 | steps/step-1.md | COMPLETED | Correct menu transition, hide path and graphics |
| 2 | steps/step-2.md | COMPLETED | Replace Cast outline with a licensed solid Cast mark |
## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Test | sh scripts/test_ui.sh | Existing host runner | yes |
| Build | make RELEASE_JOBS=4 release-build | makefile | yes |
| Package | sh scripts/package_release.sh | release script | yes |
## Context & Learnings
### Key Decisions
- Reuse existing key hint placement and hit targets.
### Gotchas & Warnings
- False from ImGui overlay currently triggers legacy cleanup.
### Working Set
| Path | Role | Evidence |
|---|---|---|
| source/main.c | Channel selection | Read main_browser_apply |
| source/player/render/imgui/imgui_overlay.cpp | Drawing and no-op status | Read |
| source/player/render/frontend.c | Fallback dispatch | Read |
### Verified Facts
- Successful play currently preserves browser and opens a drawer from Home.
- Empty ImGui overlay returns false and invokes legacy bottom cleanup.
## Implementation Log
| Date | Step | Summary |
|---|---|---|
| 2026-09-22 | 1 | Traced selection and rendering paths. |
| 2026-09-22 | 1 | Implemented fixes; host UI tests, release compilation and packaging passed. Device artifact verification remains with user. |
| 2026-09-23 | 2 | Replaced outline with Remix Cast silhouette; UI tests, build and package passed. Device visual check remains. |
