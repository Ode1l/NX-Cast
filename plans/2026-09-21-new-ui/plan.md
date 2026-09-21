# Plan: Bilingual console home and full-screen IPTV

> Status: ACTIVE
> Created: 2026-09-21
> Last Updated: 2026-09-21

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
| 2 | Pending implementation | PENDING | Full-screen channels, playback drawer, continuous navigation and remaining translations |
| 3 | Pending implementation | PENDING | Dynamic IPTV catalog capacity and startup source scanning refinements |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Build | make RELEASE_JOBS=4 release-build | makefile | yes |
| Navigation | cc -std=c11 -Isource scripts/test_iptv_channel_list.c -o /tmp/nx-ui-test && /tmp/nx-ui-test | existing test | yes |

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
| source/player/ui/channel_list.h | Shared touch geometry | inspected |
### Verified Facts
- Chinese font glyphs are already loaded; channel lists already support swipe paging.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-21 | 1 | Created new-ui branch |
| 2026-09-21 | 1 | Implemented the latest passive-cast home design; release build and home/navigation host tests pass. Device test package and instructions prepared. |
