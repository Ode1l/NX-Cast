# Plan: Bilingual console home and full-screen IPTV

> Status: COMPLETED
> Created: 2026-09-21
> Last Updated: 2026-09-22

## Goal
Deliver a buildable bilingual home and full-screen IPTV interface for device testing.

## Assumptions
- Physical rendering and controller ergonomics require user testing on Switch.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- Equal home tiles without slogan; full-screen IPTV with usable touch targets.
- English/Chinese selection follows system initially and persists on SD.
- Existing playback, source management and single Joy-Con inputs remain available.
### Non-goals
- Protocol and decoder changes.
### Edge Cases
- Missing EPG, empty sources, failed preference writes and long multilingual titles.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Home | Intro hero / equal tiles | Equal tiles | User approved |
| IPTV | Drawer / full screen | Full screen | User approved |
| Language | English / bilingual | System default and saved override | User approved |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| 1 | `steps/step-1.md` | COMPLETED | Passive cast home, actionable TV tile and home language |
| 2 | `steps/step-2.md` | COMPLETED | IPTV browser, dynamic catalog, continuous input, bilingual UI and lightweight player styling |
| 3 | `steps/step-3.md` | COMPLETED | Single-line scrolling titles and neutral timeline |
| 4 | `steps/step-4.md` | COMPLETED | SD language persistence, live controls, drawer dismissal and home icons |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Build | make RELEASE_JOBS=4 release-build | makefile | yes |
| Navigation, language, layout | make test-ui | shared host test script | yes |
| Large catalog / allocation failure | make test-iptv-data | isolated fixture test | yes |
| Package | sh scripts/package_release.sh | release packaging | yes |

## Context & Learnings
### Key Decisions
- C owns settings/input; C++ owns ImGui drawing.
### Gotchas & Warnings
- Shared hit rectangles must match rendered controls.
### Working Set
| Path | Role in this task | Evidence |
|------|--------------------|--------|
| source/main.c | Main input handling | inspected |
| source/player/render/imgui/imgui_overlay.cpp | Existing home and IPTV rendering | inspected |
| source/player/ui/browser.h | Shared browser state and touch geometry | tested |
### Verified Facts
- Chinese font glyphs are loaded; continuous input replaces the old paging UI.
- 10,041-channel, 81-source, 100-group catalog and allocation failure tests passed.
- Build and ZIP verified; physical rendering/controller acceptance remains on Switch.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-21 | 1 | Created new-ui branch |
| 2026-09-21 | 1 | Implemented the latest passive-cast home design; release build and home/navigation host tests pass. Device test package and instructions prepared. |
