# Plan: Merge new-ui into main

> Status: COMPLETED
> Created: 2026-09-23
> Last Updated: 2026-09-23

## Goal
Integrate the already released `new-ui` branch into `main` without rewriting history or moving the v0.3.1 tag.

## Assumptions
- The user explicitly requested merging the tested and released `new-ui` branch into `main`.
- Fast-forward integration is preferred if remote `main` has not advanced.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [ ] `main` includes the `new-ui` release commit and release-verification record.
- [ ] Remote `main` is updated without force-push; tag `v0.3.1` remains unchanged.
- [ ] Local worktree is clean after the merge/push.

### Non-goals
- No code changes, rebasing, force-pushing, or retagging.

### Edge Cases
- If remote `main` has advanced or the merge conflicts, stop and inspect rather than overwrite.

## Design Decisions
None — integrate existing branch history without rewriting it.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Fast-forward main to new-ui and push |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Branch ancestry | `git merge-base --is-ancestor origin/main origin/new-ui` | Git | yes |
| Merge | `git merge --ff-only new-ui` | Git | yes |
| Verify pushed state | `git ls-remote origin refs/heads/main` | Git | yes |
| Worktree | `git status -sb` | Git | yes |

## Context & Learnings
### Key Decisions
- Fast-forward only preserves the release history and avoids unnecessary merge commits.
### Gotchas & Warnings
- The annotated v0.3.1 tag points to the release commit; the branch has one later documentation-only verification commit. Do not move the tag.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| Git refs | Source/target history | inspected local/remote branches and graph |

### Verified Facts
- `origin/main` is ancestor of `origin/new-ui` at the beginning of this task — verified by fetched refs and `git log --graph` on 2026-09-23.
- `v0.3.1` points to `18c8a7e`; `new-ui` additionally contains documentation commit `3e483cc` — verified by `git log` on 2026-09-23.
- Local `main` fast-forwarded to `3e483cc`, matching `origin/new-ui`; tag `v0.3.1` was not moved — verified by Git ref inspection on 2026-09-23.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-23 | 1 | Fast-forwarded `main` to the validated `new-ui` tip without conflicts or history rewrite. |
