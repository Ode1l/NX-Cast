# Plan: macOS Handoff and GitHub Publish

> Status: COMPLETED
> Created: 2026-07-23
> Last Updated: 2026-07-23

## Goal
Publish the accumulated AirPlay/DLNA resource-management and diagnostic work with an evidence-based macOS handoff, while keeping Windows-only VS Code configuration and all generated artifacts local.

## Assumptions
- The current `airplay` branch is the intended GitHub branch because it tracks `origin/airplay` and the user asked to push the current work before moving to macOS.
- The user wants all semantic source, test, Makefile, diagnostic documentation, and completed workflow-plan changes included, not CRLF-only worktree noise.
- `.vscode/tasks.json`, `.vscode/launch.json`, and `plans/2026-07-22-vscode-space-path-build/` are Windows-local records and must remain outside every commit.
- No pull request is required; the requested terminal state is an updated `origin/airplay` branch.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] A handoff document records architecture changes, diagnostic profiles, physical test results, unresolved failures, and macOS continuation commands.
- [x] The local Windows VS Code task/launch configuration remains present but unstaged and uncommitted, with its reconstruction contract recorded in the handoff.
- [x] No `build/`, `dist/`, `sdmc/`, `logs/`, `artifacts/`, NRO, ELF, NACP, LST, or accidental `NUL` file enters a commit.
- [x] CRLF-only changes are removed from the publish diff without changing semantic content.
- [x] Relevant focused host tests and a clean Profile 14 Switch build pass, or any environmental limitation is explicitly recorded.
- [x] Curated commits are pushed to `origin/airplay`, and the remote head matches the final local commit.

### Non-goals
- Add ALAC support, change Bilibili request headers, or otherwise alter playback behavior during handoff.
- Convert Windows VS Code commands into a committed macOS task configuration.
- Open a pull request or upload generated binaries/logs.

### Edge Cases
- The dirty worktree contains more than 200 tracked paths because of line-ending conversion; only paths with semantic differences may be staged.
- GitHub CLI is absent, so authentication is verified through the connected GitHub app and the existing git remote; local `git push` remains the requested transport.
- The complete host AirPlay suite may remain unavailable on Windows because host mbedTLS/libsodium/FFmpeg and sanitizer runtimes are missing.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Publish scope | Entire dirty worktree; semantic diffs only; source subset only | Semantic code/test/docs/plans, excluding Windows-local configuration and generated files | yes — user explicitly requested records and code, with local-only task configuration and no binaries |
| Branch strategy | New branch; current branch; default branch | Push current `airplay` branch to `origin/airplay` | yes — current branch already tracks the requested remote branch |
| Handoff format | Only old plans; update context only; dedicated handoff plus context/index | Dedicated `docs/MACOS_HANDOFF_2026-07-23.md`, linked from docs and summarized in `plans/context.md` | yes — required for a usable macOS continuation |
| VS Code record | Commit tasks; delete changes; retain locally and document contract | Retain `.vscode` locally, exclude from staging, record labels/profile list and exclusion reason in handoff | yes — user explicitly said not to upload task changes |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Write the macOS handoff and update durable documentation/context. |
| Step 2 | `steps/step-2.md` | COMPLETED | Remove line-ending noise, validate the curated source tree, and prove artifact exclusions. |
| Step 3 | `steps/step-3.md` | COMPLETED | Stage only the approved manifest, commit intentionally, push, and verify the remote head. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Semantic diff inventory | `git diff --ignore-space-at-eol --name-only` | Current CRLF-heavy worktree evidence | yes |
| Focused host regressions | `make ... test-runtime-diagnostics test-network-diagnostics test-protocol-coordinator test-airplay-server-lifecycle test-airplay-mdns-suspend test-dlna-controller-session` | Existing Makefile targets | yes |
| Switch build | `make ... NXCAST_DIAG_PROFILE=full-owner-exclusive-observe-bsd12 ... -j4` | Profile 14 build contract | yes |
| Patch hygiene | `git diff --check` and `git diff --cached --check` | Git review convention | yes |
| Artifact/config exclusion | `git diff --cached --name-only` plus path-pattern checks | User publish constraint | yes |
| Remote verification | `git ls-remote --heads origin airplay` | Existing Git remote | yes |

## Context & Learnings
### Key Decisions
- Treat generated device logs as evidence summarized in documentation, never as repository artifacts.
- Preserve completed per-experiment plans because they carry the detailed design and validation record behind the consolidated handoff.

### Gotchas & Warnings
- `.vscode/tasks.json` and `.vscode/launch.json` are tracked files, so a blanket `git add -A` would violate the user's request.
- `plans/2026-07-22-vscode-space-path-build/` is untracked but Windows-specific and must remain local with the `.vscode` changes.
- The latest Profile 14 device log contains expiring signed media URLs; only redacted outcomes belong in documentation.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md` | Full diagnostic matrix and interpretation contract | Profile 14 procedure and profile rows read on 2026-07-23 |
| `plans/2026-07-22-*/` | Detailed implementation/test records | Completed plan status and Verified Facts inventory on 2026-07-23 |
| `logs/run_nxlink-20260723-002338.log` | Latest physical Profile 14 evidence | Four DLNA HTTP 514 failures and three AirPlay `ct=2` format rejections parsed on 2026-07-23 |
| `.vscode/tasks.json`, `.vscode/launch.json` | Local Windows build/upload workflow | Diff shows devkitPro MSYS paths and diagnostic picker; explicitly excluded by user |
| `makefile` | Cross-platform build/profile/test contract | Semantic diff contains Profiles 9-14 and focused host targets |
| `.gitignore` | Generated artifact exclusion | `build`, `sdmc`, `logs`, artifacts and Switch binaries are ignored |
| `source/`, `scripts/` | Accumulated resource, protocol, observability and regression work | Semantic diff inventory contains 37 production/test paths plus new diagnostic files |
| `docs/MACOS_HANDOFF_2026-07-23.md` | Consolidated macOS continuation record | Re-read after creation; redaction and link checks passed on 2026-07-23 |
| `docs/README.md`, `plans/context.md` | Handoff discovery and current-session orientation | Re-read after edit; scoped `git diff --check` passed on 2026-07-23 |
| `build/tests/`, `NX-Cast.nro` | Local-only validation artifacts | Focused tests and clean Profile 14 build passed; paths confirmed ignored on 2026-07-23 |

### Verified Facts
- `airplay` and `origin/airplay` both start at `94146ae`; the remote is `https://github.com/Ode1l/NX-Cast.git` — verified by git branch/log/remote/ls-remote, 2026-07-23.
- Of 213 tracked modified paths, only 40 contain semantic differences when end-of-line whitespace is ignored — verified by `git diff --ignore-space-at-eol`, 2026-07-23.
- Generated NRO/ELF/NACP, `build/`, `sdmc/`, and `logs/` are already ignored — verified by `.gitignore` and `git status --ignored`, 2026-07-23.
- The Windows task diff replaces `/bin/bash` with devkitPro MSYS Bash and adds Profiles 1-14 selection; it is not portable to the upcoming macOS workspace — verified by scoped `.vscode` diff, 2026-07-23.
- Profile 14 device testing reached normal resource convergence with no resource failure, socket-accounting fault, thread-creation failure, or monotonic heap leak — verified from the latest device log analysis, 2026-07-23.
- The latest media failures are distinct: all four DLNA Bilibili URLs received HTTP 514 before `file-loaded`, while all three AirPlay attempts negotiated audio-only `ct=2`, `spf=352`, `sr=44100` and were rejected at format setup with RTSP 461 — verified from `logs/run_nxlink-20260723-002338.log`, 2026-07-23.
- The handoff contains no signed Bilibili query or Windows drive-prefixed toolchain command, its local documentation links resolve, and the three edited documents pass scoped `git diff --check` — verified by `rg`, `Test-Path`, full re-read, and Git on 2026-07-23.
- LF normalization reduced 213 tracked modifications to 42 semantic paths; normal and EOL-insensitive path lists are identical and `git diff --check` passes — verified by Git on 2026-07-23.
- All six focused host regressions passed, and the clean strict Profile 14 build produced a 25,600,698-byte NRO containing the requested profile and libnx random-source markers — verified by Make, `strings`, and SHA-256 on 2026-07-23.
- The full host AirPlay suite remains unclaimed on this Windows host because Makefile discovery reports mbedTLS, libsodium, and FFmpeg host development packages absent — verified by Make database inspection on 2026-07-23.
- The curated implementation/handoff commit is `6ddb05acf065e59c787937c8fadd58a6bffc9363`; after SSH push, local HEAD, the refreshed `origin/airplay`, and `git ls-remote` all matched — verified by Git on 2026-07-23.
- `.vscode/tasks.json`, `.vscode/launch.json`, and `plans/2026-07-22-vscode-space-path-build/` remain local and outside the commit, while all build/log/Switch binary paths remain ignored — verified by post-push status and staged-path assertions on 2026-07-23.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-07-23 | Step 1 | Added the redacted macOS handoff, linked it from the documentation index, and replaced stale session context; documentation redaction, links, re-read, and whitespace checks passed. |
| 2026-07-23 | Step 2 | Removed EOL-only noise and the accidental NUL artifact, passed six focused host regressions and a clean strict Profile 14 build, and confirmed generated artifacts remain ignored. |
| 2026-07-23 | Step 3 | Explicitly staged 89 approved paths, committed as `6ddb05a`, pushed `airplay` over authenticated SSH, and verified the remote ref; local Windows configuration stayed uncommitted. |
