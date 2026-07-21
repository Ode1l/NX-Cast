# Step 1: Restore And Validate The Runtime Log Baseline

> Status: COMPLETED
> Created: 2026-07-21

## Goal
Make normal logging memory-first and WARN-only while preserving deliberate Full Trace visibility and nonblocking nxlink behavior.

## Prerequisites
- Historical v0.1.0, pre-GUI, and current logger implementations have been compared.
- User confirmed the recommended baseline.
- Files to modify: `makefile`, `source/log/log.h`, `source/log/log.c`, `source/main.c`, and focused docs/tests if required.

## Deliverables
- Normal startup performs no runtime log file creation or rotation.
- Full Trace emits periodic health and mpv INFO records through the logger.
- Normal and Full Trace builds plus logger/host tests pass.

## Plan
- [x] `edit` logger/build configuration — select WARN normally and INFO only for trace builds.
- [x] `edit` main/logger sinks — remove live SD persistence while retaining memory history and nonblocking nxlink.
- [x] `test` logger and host suites — verify queue/mirror behavior.
- [x] `build` normal and strict Full Trace Switch variants — verify both policies compile.
- [x] `bash` diff hygiene and binary/string checks — verify policy evidence.

## Quality Checklist
- [x] Evidence-before-edit: historical/current logger files read; impact search completed; validation commands recorded.
- [x] Existing pattern / reuse checked: v0.1.0 logger policy and current mirror tests.
- [x] Contract understood: producers enqueue; worker owns history/mirror; normal release avoids SD I/O.
- [x] Risk reviewed: nxlink backpressure, trace flooding, shutdown ordering, dirty worktree.
- [x] Mitigation recorded: retain nonblocking mirror, bounded queue, and dual build validation.

## Validation Checklist
- [x] Normal Switch build exits 0.
- [x] Strict Full Trace Switch build exits 0.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] `make test-log-mirror` passes.
- [x] `make test-airplay` passes.

## Implementation Notes
- Protection commit/stash was intentionally skipped because the worktree contains broad user-owned and ongoing AirPlay/player changes.
- `NXCAST_LOG_LEVEL_DEFAULT` is WARN normally and INFO only when `NXCAST_TRACE_BUILD=1`; the Makefile derives that flag from any media/input/AirPlay Trace option.
- The logger worker keeps bounded in-memory history and optionally mirrors to nxlink with nonblocking `send()`. It no longer opens, rotates, flushes, or writes `runtime.log`.
- Logger health reports records `processed` by the worker rather than the old file-oriented `persisted` and `file_*` fields.
- Logger startup now precedes storage/network setup; nxlink remains enabled only after networking is ready.
- Host policy/mirror and aggregate AirPlay tests passed. Normal and Full Trace strict Switch builds passed; the final local NRO is the normal build with SHA-256 `ef29d74729aae9af8df6fe2f1fb5616dc890cb97b71fbe53e350aba4bf9dd3e1`.

## Files Changed
- `makefile`
- `source/log/log.h`
- `source/log/log.c`
- `source/main.c`
- `scripts/test_log_policy.c`
- `docs/threading-design.md`
- `plans/2026-07-21-v010-log-baseline/plan.md`
- `plans/2026-07-21-v010-log-baseline/steps/step-1.md`
