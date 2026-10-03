# Step 1: Update README introduction

> Status: COMPLETED
> Created: 2026-09-24

## Goal
Make the opening README description concise, current, and consistent with the media-center positioning.

## Prerequisites
- User selected the media-center positioning.
- Current README opening and nearby feature claims were inspected.

## Deliverables
- Updated README introduction and Home-screen status line.

## Plan
- [x] Edit `README.md` opening and outdated Home-screen bullet.
- [x] Review README diff and run `git diff --check`.

## Quality Checklist
- [x] Evidence-before-edit: README read, wording search completed, diff validation identified.
- [x] Existing pattern / reuse checked: reuse current feature sections instead of duplicating details.
- [x] Contract understood: describe shipped DLNA/IPTV and experimental AirPlay accurately.
- [x] Risk reviewed: marketing copy could overstate support.
- [x] Mitigation recorded: keep AirPlay qualifier and user-provided playlist wording.

## Validation Checklist
- [x] `git diff --check`
- [x] `git diff -- README.md`

## Test Checklist
- [x] N/A — documentation-only wording change.

## Implementation Notes
The product description now leads with "media center" and identifies DLNA/IPTV as current modes and AirPlay as experimental. The outdated static-Home claim was updated.

## Files Changed
- `README.md`
