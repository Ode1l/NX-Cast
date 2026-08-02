# Plan: VS Code Diagnostic Workflow Documentation Publish

> Status: ACTIVE
> Created: 2026-08-02
> Last Updated: 2026-08-02

## Goal
Publish a durable Markdown record of the local VS Code diagnostic launch workflow and existing test handoff while leaving all environment-specific build, job, task, and launch configuration changes local.

## Assumptions
- The current `airplay` branch is the intended push target because local HEAD and `origin/airplay` both point to `77cec6077d7bda9cc698881f9dd6408aa5767dc1`.
- `.vscode/tasks.json` is environment-specific alongside `.vscode/launch.json`, so it is excluded even though the user named jobs and launch explicitly.
- The completed Windows space-path plan remains local because its exact Make/MSYS workaround is not portable; only its reusable launch/task contract is summarized in public documentation.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [ ] A Markdown document records the new diagnostic launch name, its pre-launch task, profile picker, execution flow, and platform adaptation rules.
- [ ] The existing AirPlay/DLNA test handoff remains the source of truth and links to the new VS Code workflow record.
- [ ] `makefile`, `.github/workflows/*`, `.vscode/tasks.json`, and `.vscode/launch.json` do not enter either commit.
- [ ] The local Windows VS Code changes and `plans/2026-07-22-vscode-space-path-build/` remain present after publication.
- [ ] The final local, tracking, and remote `airplay` SHAs match.

### Non-goals
- Change any Makefile rule, GitHub Actions job, VS Code JSON file, runtime code, diagnostic profile, or playback behavior.
- Claim that the Windows MSYS command is valid on macOS or another workspace layout.
- Create a pull request or upload generated binaries/logs.

### Edge Cases
- The two `.vscode` files are tracked and modified, so blanket staging would publish them accidentally.
- The older Windows path plan is untracked and must not be swept into the documentation commit.
- The repository currently has no semantic `makefile` or `.github/workflows` diff, but staged-path assertions must still protect them.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Record location | Extend only the July handoff; commit local VS Code JSON; add a standalone portable workflow document | Add `docs/LOCAL_VSCODE_DIAGNOSTIC_WORKFLOW.md` and link it from the handoff/index | yes — user requested documentation instead of publishing environment-specific files |
| Configuration detail | Copy exact Windows paths; omit launch details; describe stable labels/contracts plus platform substitutions | Record exact VS Code labels/profile names and portable execution contract, but use placeholders for platform paths/Make invocation | yes — user warned that the next environment compiles differently |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Write and link the portable diagnostic launch workflow record. |
| Step 2 | `steps/step-2.md` | IN_PROGRESS | Explicitly stage documentation only, commit, push, and verify exclusions and remote state. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Local config syntax | `Get-Content -Raw .vscode/{tasks,launch}.json | ConvertFrom-Json` | Existing local VS Code configuration | yes |
| Documentation links/details | `rg` exact launch/task/profile labels and `Test-Path` linked documents | Local config and docs index | yes |
| Patch hygiene | `git diff --check` and `git diff --cached --check` | Repository convention | yes |
| Exclusion audit | `git diff --cached --name-only` plus forbidden-path assertions | User publish constraints | yes |
| Remote verification | `git ls-remote --heads origin airplay` compared with local/tracking SHA | Existing Git remote | yes |

## Context & Learnings
### Key Decisions
- Treat VS Code task/launch configuration as a local adapter; document the stable task graph and profile contract in Git.
- Preserve `docs/MACOS_HANDOFF_2026-07-23.md` as the test-result source of truth instead of duplicating the full Profile 1-14 matrix.
### Gotchas & Warnings
- Never use `git add -A` while the tracked `.vscode` files are modified.
- “Jobs” is interpreted conservatively as both GitHub workflow jobs and VS Code tasks; neither is published.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `.vscode/launch.json` | Local source for the new diagnostic launch contract; excluded from commit | Semantic diff read on 2026-08-02 |
| `.vscode/tasks.json` | Local source for task/profile picker contract; excluded from commit | Semantic diff and JSON parse read on 2026-08-02 |
| `docs/MACOS_HANDOFF_2026-07-23.md` | Existing test results and macOS continuation record | Local VS Code Task Contract section read on 2026-08-02 |
| `docs/README.md` | Documentation discovery index | Full file read on 2026-08-02 |
| `plans/2026-07-22-vscode-space-path-build/` | Windows-only historical evidence; excluded from commit | Plan and step read on 2026-08-02 |
| `docs/LOCAL_VSCODE_DIAGNOSTIC_WORKFLOW.md` | Portable launch/task reconstruction record | Full re-read, exact-label search, link and drive-path checks passed on 2026-08-02 |
| `plans/context.md` | Current handoff state and macOS next action | Full re-read and scoped `git diff --check` passed on 2026-08-02 |

### Verified Facts
- The only tracked worktree changes before this task are `.vscode/launch.json` and `.vscode/tasks.json`; only the Windows path plan is untracked — verified by Git status/diff on 2026-08-02.
- The new launch is named `NX-Cast: Network Resource Diagnostic Matrix` and delegates to `NX-Cast: AirPlay Diagnostic Profile Rebuild + Nxlink Upload + Server` — verified from the local launch/task JSON diff on 2026-08-02.
- The diagnostic picker contains Profiles 1-14 and defaults to `full-owner-exclusive-observe-bsd12` — verified from `.vscode/tasks.json` on 2026-08-02.
- Both local JSON files parse successfully; `makefile` and `.github/workflows` have no worktree diff — verified by PowerShell and Git on 2026-08-02.
- Local HEAD and remote `airplay` begin at `77cec6077d7bda9cc698881f9dd6408aa5767dc1` — verified by `git rev-parse` and `git ls-remote` on 2026-08-02.
- The documented launch/pre-launch labels match parsed local JSON; the picker has 14 entries and defaults to `full-owner-exclusive-observe-bsd12` — verified by PowerShell, `rg`, and full document re-read on 2026-08-02.
- All three documentation links resolve, the new record contains no drive-prefixed Bash executable, and the four Markdown targets pass `git diff --check` — verified by `Test-Path`, `rg`, and Git on 2026-08-02.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-02 | Step 1 | Added and linked the portable VS Code diagnostic workflow record; restored local JSON changes and passed contract, link, redaction, re-read, and whitespace validation. |
