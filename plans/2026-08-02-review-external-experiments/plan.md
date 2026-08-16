# Plan: Review External Experiments

> Status: COMPLETED
> Created: 2026-08-02
> Last Updated: 2026-08-02

## Goal
Read and understand the repository changes made on another computer, with particular attention to documentation and its relationship to the current code.

## Assumptions
- The current working tree contains the changes the user wants reviewed.
- This task is read-only except for workflow bookkeeping under this plan directory.

## Open Questions
None.

## Spec-Lite
N/A — covered by Goal, Deliverables, and Validation.

## Design Decisions
None — no design-sensitive changes.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Inventory and read documentation, experiments, and corresponding code changes. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Working-tree inventory | `git status --short` and `git diff --stat` | Git | yes |
| Commit inventory | `git log` and comparison against recent commits | Git | yes |
| Documentation inventory | `rg --files -g '*.md'` and bounded reads | Repository | yes |
| Change consistency | `git diff --check` | Git | no — no product edits are planned |

## Context & Learnings
### Key Decisions
- Review documentation first, then trace its claims into code and configuration.

### Gotchas & Warnings
- The repository may already contain unrelated dirty work; do not revert, format, or modify it.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `docs/` | Current architecture, experiment, and operational documentation | Read the changed diagnostic, handoff, and local workflow documents through commit `29d9901`. |
| `plans/` | Persisted experimental plans and implementation outcomes | Read the changed experiment plans for profiles 9-14, resource ownership, heartbeat compaction, audio-first handling, and publishing. |
| repository history and working tree | Identifies changes made on the other computer | Compared `94146ae..29d9901`; the branch matched `origin/airplay` before this workflow record was created. |
| `source/app/` | Protocol ownership, resource transitions, and diagnostics | Inspected coordinator, runtime observability, and network diagnostic implementations referenced by the documents. |
| `source/protocol/` and `source/player/` | AirPlay, DLNA, IPTV, and playback behavior | Inspected the corresponding lifecycle, controller-session, suspension, bridge, and libmpv diagnostic changes. |

### Verified Facts
- User requested a read-only review with particular emphasis on documentation — confirmed in chat, 2026-08-02.
- The reviewed delta contains 93 changed paths between `94146ae` and `29d9901`, led by the implementation commit `6ddb05a` and followed by documentation-only publish records.
- Profiles 13 and 14 implement first-owner exclusive resource coordination; Profile 14 preserves Profile 13 behavior and adds bounded observability.
- The latest documented AirPlay run completed pairing, FairPlay, timing, and RTSP but negotiated audio-only ALAC with no mirror stream; video still requires a mirror or remote-video path.
- The latest documented DLNA failure was an upstream HTTP 514 before `file-loaded`, so a stable LAN H.264/AAC baseline remains the required comparison test.
- No product source, build configuration, or test file was modified during this review.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-02 | Step 1 | Read all changed documentation and experiment records, then traced their claims through the coordinator, protocol lifecycle, diagnostics, player, build-profile, and focused-test code. |
