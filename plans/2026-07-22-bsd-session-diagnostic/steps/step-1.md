# Step 1: Add Isolated BSD Session Profile

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Produce a Switch NRO whose only behavioral difference from `mdns-receive` is an eight-session libnx BSD pool.

## Prerequisites
- Evidence identifies concurrent socket-service starvation as the next hypothesis.
- Files to modify: `source/main.c`, `makefile`, `.vscode/tasks.json`, and `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md`.
- Design: retain default mDNS priority and receive-only behavior.

## Deliverables
- Compile-time socket-session override with an explicit startup log.
- A selectable `mdns-receive-bsd8` profile documented as an isolation experiment.
- After this step: a strict profile build produces `NX-Cast.nro` for device testing.

## Plan
- [x] `edit` `source/main.c` — copy the default libnx socket configuration and override `num_bsd_sessions` only when requested.
- [x] `edit` `makefile` — add `mdns-receive-bsd8` with receive-only mode, default priority, and eight BSD sessions.
- [x] `edit` `.vscode/tasks.json` — expose the new profile in the existing picker.
- [x] `edit` `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md` — document the isolated hypothesis and expected result.
- [x] `bash` strict diagnostic build — require successful NRO generation.
- [x] `bash` JSON parsing and `git diff --check` — require zero errors.

## Quality Checklist
- [x] Evidence-before-edit: targets read; impact search covered profile names and socket initialization; strict build command identified.
- [x] Existing pattern / reuse checked: extend the existing profile matrix and libnx default configuration rather than creating a parallel network initializer.
- [x] Contract understood: existing profiles keep default initialization; only the new profile uses eight sessions.
- [x] Risk reviewed: socket memory budget and accidental production behavior change.
- [x] Mitigation recorded: diagnostic-only compile definition and device validation before promotion.

## Validation Checklist
- [x] Strict `mdns-receive-bsd8` build exits 0 and produces `NX-Cast.nro`.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] VS Code JSON parses and the new option appears exactly once.
- [x] Binary markers contain `mdns-receive-bsd8` and the socket-session startup log format.

## Implementation Notes
Added a compile-time override that copies libnx's default socket configuration and changes only `num_bsd_sessions`. Existing builds continue to call `socketInitializeDefault()`. The new profile uses receive-only mDNS mode, the existing default mDNS priority, and eight BSD sessions. A first incremental build was intentionally rejected because global Profile CFLAGS do not invalidate all existing objects; the final validation used a sourced `switchvars.sh`, `make clean`, and a full rebuild. The workflow's protective commit was skipped because the worktree already contains the user's and earlier task changes; no stash or unrelated commit was created.

## Files Changed
- `source/main.c`
- `makefile`
- `.vscode/tasks.json`
- `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md`
- `plans/2026-07-22-bsd-session-diagnostic/plan.md`
- `plans/2026-07-22-bsd-session-diagnostic/steps/step-1.md`
