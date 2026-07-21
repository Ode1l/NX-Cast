# Step 5: Safety Gate, Documentation, And Cross-Build

> Status: COMPLETED
> Created: 2026-07-21

## Goal
Turn the refactor into a repeatable project safety gate and verify normal/Trace Switch artifacts.

## Prerequisites
- Steps 1-4 completed with focused tests passing.
- Final changed-file and contract inventory available.

## Deliverables
- One aggregate host safety target and documented C ownership/buffer rules.
- Normal and Full Trace strict Switch builds pass from clean states.
- After this step: the plan has complete evidence and no known validation failures.

## Plan
- [x] `edit` `makefile` — finalize `test-c-safety` dependencies and optional sanitizer invocation documentation.
- [x] `write` `docs/c-safety.md` — document owned-value, truncation, checked-size, parser, and cleanup conventions.
- [x] `bash` `make test-c-safety` and `make test-airplay` — run all host regressions.
- [x] `bash` clean strict normal and Full Trace Switch builds — validate both compile-time paths.
- [x] `bash` `git diff --check` and targeted `rg` audit — verify hygiene and remaining risk inventory.

## Quality Checklist
- [x] Evidence-before-edit: all changed modules and final commands reviewed.
- [x] Existing pattern / reuse checked: documentation points to actual helpers/APIs.
- [x] Contract understood: safety gate is host-runnable and strict builds remain authoritative.
- [x] Risk reviewed: CI duration, platform sanitizer support, and dirty worktree.
- [x] Mitigation recorded: sanitizer remains explicit; normal aggregate stays portable.

## Validation Checklist
- [x] Normal strict Switch build exits 0.
- [x] Full Trace strict Switch build exits 0.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] `make test-c-safety` and `make test-airplay` pass.
- [x] ASan/UBSan safety aggregate passes with `detect_leaks=0`.

## Implementation Notes
- Added standalone DNS and seek parser tests to the portable aggregate safety gate.
- Added `make test-c-safety-sanitize` with platform-compatible ASan/UBSan settings.
- Documented ownership, failure atomicity, truncation, collection publication, cleanup, and thread-boundary rules.
- The final audit found and fixed pre-check signed overflow and non-finite floating conversion in seek target parsing.
- Clean strict normal and Full Trace builds passed; the final artifact was rebuilt as the normal variant.

## Files Changed
- `makefile`
- `docs/README.md`
- `docs/c-safety.md`
- `source/player/seek_target.c`
- `scripts/test_seek_target.c`
- `scripts/test_media_actor.c`
