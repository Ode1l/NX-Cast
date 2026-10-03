# Plan: Refresh README product introduction

> Status: COMPLETED
> Created: 2026-09-24
> Last Updated: 2026-09-24

## Goal
Describe NX-Cast as a Nintendo Switch media center while keeping current feature claims accurate.

## Assumptions
- The agreed product wording is "media center"; protocol details belong in the feature description.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [ ] README introduction leads with the media-center positioning.
- [ ] DLNA and IPTV are presented as current capabilities; AirPlay is labeled experimental.
- [ ] Home-screen description reflects the current bilingual UI.
### Non-goals
- No product, build, or release changes.
### Edge Cases
- Avoid implying bundled channels, subscription access, or certified AirPlay support.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Product description | Protocol list / media center | Media center with capabilities below | User requested |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| 1 | `steps/step-1.md` | COMPLETED | Update and verify README introduction |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Whitespace and diff | `git diff --check` | Git | yes |
| Wording review | `git diff -- README.md` | Git | yes |

## Context & Learnings
### Key Decisions
- Keep detailed protocol explanations in their existing README sections.
### Gotchas & Warnings
- AirPlay must remain explicitly experimental.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `README.md` | Product introduction and status | Inspected opening section and wording search |
### Verified Facts
- README already explains IPTV and experimental AirPlay in dedicated sections, verified by file inspection.
- The updated introduction labels AirPlay experimental and describes user-provided IPTV playlists, verified by `git diff -- README.md`.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-24 | 1 | Updated README positioning and Home status description; diff and whitespace validation passed. |
