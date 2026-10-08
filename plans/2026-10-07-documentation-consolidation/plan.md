# Plan: Consolidate Setup And Support Documentation

> Status: COMPLETED
> Created: 2026-10-07
> Last Updated: 2026-10-07

## Goal
Provide one current dependency recipe, concise bilingual build entry points and actionable privacy-aware bug reporting guidance.

## Assumptions
- Windows native verification remains pending.

## Open Questions
None.

## Spec-Lite
N/A - covered by Goal, Deliverables, and Validation.

### Acceptance Criteria
- [x] No recommended installation step installs obsolete FFmpeg first.
- [x] Platform-specific package commands are explicit; README instructions link to the maintained recipe.
- [x] Bug reports request useful evidence without publishing secrets.

### Non-goals
- Runtime changes, dependency installation, CI gates, commits or releases.

### Edge Cases
- Windows commands must run in configured MSYS2, not PowerShell or Git Bash.

## Design Decisions
None - user approved consolidation of existing documentation.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Consolidate setup and support documentation. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Whitespace | `git diff --check` | Git | yes |
| Content | Scoped diff and local Markdown link checks | Existing documents | yes |
| Build | N/A, documentation only | Scope | no |

## Context & Learnings
### Key Decisions
- Keep dependency instructions in the toolchain guide and workflow instructions in developer-workflow.md.
### Gotchas & Warnings
- Preserve unrelated uncommitted work and released version metadata.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| docs/ffmpeg-mpv-toolchain.md | Dependency instructions | Read before edits |
| README.md, README_CN.md | Bilingual entry points | Read before edits |
| docs/install.md | User support guidance | Read before edits |

### Verified Facts
- The toolchain guide installs FFmpeg 7.1-1 before 7.1-4, while README already installs the pinned package directly, verified 2026-10-07.
- A shared Bash entry and PowerShell adapter already exist; no additional script is needed.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-07 | 1 | Consolidated four documents; local links/anchors, fences, 13 Bash syntax examples and whitespace passed. No installation, native Windows test or publication performed. |
