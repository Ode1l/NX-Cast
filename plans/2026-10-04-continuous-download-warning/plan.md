# Plan: Continuous download warning

> Status: COMPLETED
> Created: 2026-10-04
> Last Updated: 2026-10-04

## Goal
Warn ordinary users away from Continuous in the current GitHub release and future CI updates without changing playback or official release assets.

## Assumptions
- Periodic IPTV stutter has no reported version, stream URL or device trace; its cause and the user's download choice are unknown.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] Current release title and first body paragraph say DO NOT DOWNLOAD and point to latest stable release.
- [x] Local CI uses the same dedicated warning, retains development commit metadata and prerelease status, and never marks Continuous latest.
- [x] Stable release assets and tags stay unchanged.
### Non-goals
- Playback/cache modifications, uploading binaries, commits or pushes of the dirty worktree, extra CI checks.
### Edge Cases
- Rolling CI must not overwrite the warning with stable release notes; stable download link must survive future versions.

## Design Decisions
None - user explicitly requested a don't-download label for development releases.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | steps/step-1.md | COMPLETED | Dedicated warning, CI update, current release metadata and verification |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Syntax and notes | ruby YAML load and isolated generated-notes assertions | build.yml shell block; installed Ruby | yes |
| Whitespace | git diff --check | Git | yes |
| Live metadata | gh release view/list --repo Ode1l/NX-Cast | installed gh CLI | yes |
| Device build | N/A - no playback or build commands changed | scoped diff | no |

## Context & Learnings
### Key Decisions
- Dedicated development notes instead of copying official v0.3.1 notes; use releases/latest for stable downloads.
### Gotchas & Warnings
- Local continuous tag is stale: use GitHub compare API for remote release comparison.
- Existing worktree contains unrelated changes that must be preserved.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| .github/workflows/build.yml | Rolling publisher | cat: title Continuous, prerelease true, body copies official notes |
| .github/continuous-release-notes.md | Dedicated warning | New scoped file replacing stable body source |
| README.md | Public release description | rg: old title NX-Cast Continuous |
### Verified Facts
- GitHub API: latest official release is v0.3.1; Continuous is a prerelease at 598fe45.
- GitHub compare v0.3.1...continuous shows three commits, toolchain/docs changes and no source/player or source/iptv code changes.
- Both release workflows invoke release-build, not a Full Trace build.
- User confirmed download version and stream details are only guesses, not reported evidence.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-04 | Step 1 | Updated current online release title/body; local CI and README warning updated; notes generation and metadata readback passed. Future CI persistence requires committing/pushing these scoped files; no unrelated dirty worktree changes were committed or pushed. |
