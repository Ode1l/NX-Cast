# Step 1: Consolidate Setup And Support

> Status: COMPLETED
> Created: 2026-10-07

## Goal
Remove outdated setup guidance and document useful failure reports.

## Prerequisites
- User approved the four proposed documentation improvements.
- Read four target documents and current build adapters.

## Deliverables
- Updated toolchain guide, bilingual README build sections and installation troubleshooting.

## Plan
- [x] Edit existing documents, retaining historical information clearly labeled.
- [x] Validate diff, links, code fences and shell example syntax without installing dependencies.

## Quality Checklist
- [x] Evidence-before-edit: target documents and scripts read.
- [x] Existing pattern / reuse checked: shared developer-workflow.md.
- [x] Contract understood: documentation only.
- [x] Risk reviewed: misleading platform commands and private diagnostic data.
- [x] Mitigation recorded: explicit platforms, privacy warning, Windows validation limitation.

## Validation Checklist
- [x] git diff --check passes.
- [x] Local links, shell examples and changes reviewed.

## Test Checklist
- N/A: runtime code unchanged; no installation or hardware test requested.

## Implementation Notes
Verified package names/order against Dockerfile and the pinned fetch script, and SDK loading against existing adapters. Recommended setup no longer installs FFmpeg 7.1-1. README instructions link to the recipe, with explicit Bash/PowerShell entry points. Added privacy-aware failure reporting and matching-ELF retention. A one-off validator checked local links/anchors, fences and 13 Bash snippets using bash -n only. No CI checks or runtime changes added.

## Files Changed
- README.md
- README_CN.md
- docs/ffmpeg-mpv-toolchain.md
- docs/install.md
- plans/2026-10-07-documentation-consolidation/plan.md
- plans/2026-10-07-documentation-consolidation/steps/step-1.md
