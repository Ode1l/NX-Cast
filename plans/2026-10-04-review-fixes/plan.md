# Plan: Fix approved storage and build findings

> Status: COMPLETED
> Created: 2026-10-04
> Last Updated: 2026-10-04

## Goal
Fix review findings 1, 3 and 4 without changing the protocol coordinator or playback behavior.

## Assumptions
- User explicitly deferred finding 2 and authorized the other three fixes.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] Failed IPTV configuration replacement preserves the previous data or its recovery backup.
- [x] VS Code release packaging builds through release-build.
- [x] APP_VERSION participates in incremental build invalidation.
### Non-goals
- Coordinator, protocol, decoder, playback state machine, new CI gates, publishing.
### Edge Cases
- SD rename cannot overwrite; replacement and rollback failures; first save.

## Design Decisions
None - local logic/config fixes using the existing backup/rollback pattern.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | steps/step-1.md | COMPLETED | Protect IPTV configuration saves and exercise failure paths |
| Step 2 | steps/step-2.md | COMPLETED | Align release task and version invalidation, verify build |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Storage tests | make test-iptv-data | Existing host test target | yes |
| Dependency tests | python3 scripts/test_ffmpeg_install.py | Existing isolated fixtures | yes |
| Build/package | make release-build RELEASE_JOBS=4; scripts/package_release.sh | Existing release entry | yes |
| Scope | git diff --check; git diff --stat | Git | yes |

## Context & Learnings
### Key Decisions
- Use one private IPTV replacement helper; do not generalize storage across unrelated modules.
### Gotchas & Warnings
- Never delete the backup after failed rollback; do not claim power-loss atomicity on SD.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| source/iptv/iptv.c | Three configuration writers | Reviewed delete-before-rename paths |
| scripts/test_iptv_data.c | Host fixtures and fault injection | Includes production IPTV implementation |
| .vscode/tasks.json, makefile | Release entry and build signature | Prior review reproduced both issues |
### Verified Facts
- home_ui_toggle_language already uses backup/rollback for SD replacement.
- Worktree contains only previous review records before edits.
- IPTV fault-injection and catalog tests passed; installer fixture tests passed (10).
- Real release build and package succeeded; installed FFmpeg 7.1-4 was reused. No compiler warnings/errors found in the captured build log.
- Temporary signature fixture verified same-version retention and changed-version invalidation without altering the real 0.3.2 version.
- Git diff confirms coordinator, protocols, player and GitHub workflows unchanged. Hardware validation not performed.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-04 | Step 1 | IPTV configuration replacement and error injection tests passed. |
| 2026-10-04 | Step 2 | Release task, version signature, full build and packaging validated; no publish. |
