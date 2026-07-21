# Plan: Restore v0.1.0 Logging Baseline

> Status: COMPLETED
> Created: 2026-07-21
> Last Updated: 2026-07-21

## Goal
Restore the low-volume, memory-first v0.1.0 logging policy while retaining nonblocking nxlink delivery and Full Trace diagnostics.

## Assumptions
- The user's approval applies to the historical baseline recommended in the preceding audit.
- Existing dirty worktree changes belong to ongoing work and must not be reset or stashed.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] Normal builds default to WARN/ERROR and do not create or write a runtime SD log.
- [x] Full Trace builds retain INFO heartbeat, media/input traces, and mpv INFO diagnostics.
- [x] Producers remain nonblocking and nxlink mirror failures remain retryable.
- [x] Focused host tests and strict Full Trace Switch build pass.

### Non-goals
- Removing shutdown-only trace diagnostics.
- Changing playback, protocol ownership, or media actor behavior.

### Edge Cases
- nxlink is absent, backpressured, or closes while the application continues.
- Logs are emitted before nxlink becomes available.
- Full Trace produces more records than the mirror can accept.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Runtime persistence | Per-record SD log; nxlink-primary SD fallback; memory-only | Memory history plus optional nxlink; shutdown trace remains separate | yes — user approved trying the v0.1.0 baseline |
| Normal log level | INFO; WARN | WARN | yes — matches v0.1.0 recommendation |
| Full Trace routing | Global DEBUG; targeted WARN; build-time INFO | Build-time INFO with targeted heartbeat/media/input and mpv INFO | yes — preserves current diagnostic requirements without affecting release builds |
| nxlink writer | Blocking stdio; nonblocking send | Keep nonblocking send and retry counters | yes — do not restore the historical blocking risk |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Apply the v0.1.0 runtime policy and validate normal/trace builds. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Logger socket tests | `make test-log-mirror` | Existing Makefile target | yes |
| Host regression | `make test-airplay` | Existing aggregate target | yes |
| Normal Switch build | `make NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 -j4` | Existing release contract | yes |
| Full Trace Switch build | `make TRACE_MEDIA=1 TRACE_INPUT=1 TRACE_AIRPLAY=1 NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 -j4` | Existing trace task | yes |
| Diff hygiene | `git diff --check` | Repository convention | yes |

## Context & Learnings
### Key Decisions
- Use the v0.1.0 policy, not its blocking stderr implementation.
- Compile-time trace selection controls verbosity from the single logger source of truth.
### Gotchas & Warnings
- `runtime.log` was useful after nxlink loss but reintroduced Switch filesystem I/O into the live logger.
- The worktree contains broad ongoing AirPlay/player changes.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/log/log.h` | Global default level | v0.1.0 uses WARN; current worktree uses INFO. |
| `source/log/log.c` | Queue, history, sinks, and health | Current worker owns both socket and optional SD fallback. |
| `source/main.c` | Logger setup and heartbeat | Current startup creates/rotates runtime SD logs before starting the worker. |
| `source/player/backend/libmpv.c` | mpv trace verbosity | Current Full Trace preserves mpv INFO explicitly. |
| `source/player/trace.c` | Media trace routing | Current trace events use INFO. |
| `makefile` | Normal/Trace policy selection and host test entry point | Any enabled Trace flag now defines `NXCAST_TRACE_BUILD=1`. |
| `scripts/test_log_policy.c` | Compile-time policy regression test | Asserts WARN for normal builds and INFO for Trace builds. |
| `docs/threading-design.md` | Runtime logging ownership contract | Documents memory history and optional nonblocking nxlink without live SD I/O. |

### Verified Facts
- v0.1.0 uses a 2048-entry queue, one 32 KiB worker, a 4096-entry memory history, standard nxlink stderr, and no runtime SD sink — verified with `git show v0.1.0:source/log/log.c`, 2026-07-21.
- v0.1.0 defaults to WARN and promotes selected media/input trace records through the WARN threshold — verified with `git show v0.1.0:source/log/log.h`, `source/player/trace.c`, and `source/main.c`, 2026-07-21.
- The first C player UI commit did not modify logger source; Dear ImGui was introduced later in `83fc80f` — verified by git history, 2026-07-21.
- Normal strict Switch build excludes `runtime-heartbeat`, `runtime.log`, and `runtime.previous` strings; SHA-256 `ef29d74729aae9af8df6fe2f1fb5616dc890cb97b71fbe53e350aba4bf9dd3e1` — verified 2026-07-21.
- Full Trace strict Switch build includes the runtime heartbeat and excludes runtime SD log paths; SHA-256 `6d9a5defa9e313efcdad20e33a7ee4e5efd10443d3b4cea39e5f94075d5e2fbd` — verified 2026-07-21.
- Logger policy/mirror tests, the aggregate AirPlay host suite, normal build, Full Trace build, and `git diff --check` all pass — verified 2026-07-21.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-07-21 | Step 1 | Restored WARN-by-default memory-first logging, retained nonblocking nxlink, enabled INFO only for Trace builds, removed live runtime SD logging, and validated both Switch variants plus host suites. |
