# Plan: Runtime Log Reliability

> Status: COMPLETED
> Created: 2026-07-21
> Last Updated: 2026-07-21

## Goal
Keep diagnostic logs available after nxlink disconnects and make IPTV loading/home-return hangs identifiable from ordered runtime events.

## Assumptions
- The latest nxlink log is representative of the current Full Trace build.
- Existing dirty worktree changes are user work and must not be stashed, reset, or overwritten.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] Full Trace writes an SD-backed current runtime log independently of nxlink.
- [x] nxlink mirroring cannot block the log worker indefinitely after a socket failure.
- [x] Main-loop, player-stop, home-return, IPTV load, and protocol-owner events can be ordered from logs.
- [x] Host tests and a clean Full Trace Switch build pass.

### Non-goals
- Fixing the underlying IPTV playback or home-return hang before the new trace identifies its blocking boundary.
- Changing player, IPTV, DLNA, or AirPlay behavior.

### Edge Cases
- nxlink resets while the application continues running.
- The SD log path cannot be opened or the log queue overflows.
- The main thread blocks before a paired `done` event is emitted.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Reliable trace destination | nxlink only; SD only; SD primary plus nxlink mirror | SD primary plus non-blocking nxlink mirror | yes — required to solve missing logs while preserving live output |
| Runtime heartbeat | every frame; periodic; state changes only | periodic health heartbeat plus state-change events | yes — avoids frame-level noise |
| Persistent file lifecycle | append forever; truncate each run; rotate current to previous | rotate current to previous and create a new current log | yes — bounds file growth and preserves the prior crash run |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add SD persistence and non-blocking nxlink mirroring |
| Step 2 | `steps/step-2.md` | COMPLETED | Add ordered hang-boundary and heartbeat events |
| Step 3 | `steps/step-3.md` | COMPLETED | Validate tests, Full Trace build, and log coverage |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Diff hygiene | `git diff --check` | repository convention | yes |
| Coordinator test | `make test-protocol-coordinator` | `makefile` target | yes |
| AirPlay tests | `make test-airplay` | `makefile` target | yes |
| Full Trace build | `source /opt/devkitpro/switchvars.sh && make clean && make TRACE_MEDIA=1 TRACE_INPUT=1 TRACE_AIRPLAY=1 NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 -j4` | `.vscode/tasks.json` | yes |

## Context & Learnings
### Key Decisions
- Persist before mirroring: an unavailable nxlink peer must not prevent an SD log write.
- Keep logging asynchronous: protocol and render threads only enqueue formatted records.

### Gotchas & Warnings
- `nxlinkStdioForDebug()` returned fd 3 in the latest run and the peer reset during AirPlay startup.
- The existing logger mirrors with blocking `fprintf(stderr)` and has no persistent file sink.
- Do not log AirPlay keys, PIN values, URLs with credentials, or media payloads.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260721-185601.log` | Latest failed trace | `nl -ba` shows reset after AirPlay ed25519 startup |
| `source/log/log.c` | Async queue and nxlink mirror | `sed` shows blocking stderr mirror and in-memory history only |
| `source/log/log.h` | Logger public API | `rg` confirms single global level/API source |
| `source/main.c` | Logger setup, main loop, and home-return path | `nl`/`rg` locate nxlink fd and `player_stop()` boundary |
| `source/player/core/session.c` | Player command queue and stop path | `rg`/`sed` locate existing media trace calls |
| `source/iptv/iptv.c` | IPTV load/channel-switch boundary | `rg` locates stop/set-uri/play sequence |

### Verified Facts
- The latest trace ends with `recv: Connection reset by peer` before the main-loop startup messages — verified by `nl -ba logs/run_nxlink-20260721-185601.log`, 2026-07-21.
- The logger currently stores history in memory and mirrors through `fprintf(stderr)`/`fflush(stderr)` only — verified by reading `source/log/log.c`, 2026-07-21.
- `log_runtime_init()` runs before the NX-Cast SD directory is prepared — verified at `source/main.c:990-1000`, 2026-07-21.
- Return-home calls `player_stop()` synchronously before its caller emits the result log — verified in `source/main.c`, 2026-07-21.
- Superseding physical evidence showed the original dual-sink implementation was not reliable on Switch: per-line SD flush could hold the sink mutex while main-thread health sampling waited on it, stopping both live output and rendering. The corrective work is tracked by runtime concurrency Step 6 — verified by July 21 hardware A/B runs, historical commit `ce0257b`, and current source inspection, 2026-07-21.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-07-21 | Step 1 | Added rotating SD runtime log, non-blocking nxlink socket mirror, and logger health counters; incremental Switch build passed. |
| 2026-07-21 | Step 2 | Added runtime heartbeat and ordered return-home, player backend, libmpv mutex, and IPTV load-stage events; normal Switch build passed. |
| 2026-07-21 | Step 3 | All host protocol tests, diff hygiene, and clean Full Trace Switch build passed; generated 25,596,602-byte NRO. |
| 2026-07-21 | Superseding hardware correction | Replaced simultaneous per-record SD + nxlink writes with nxlink-primary/SD-fallback policy, removed sink locks from health snapshots, and added zero-timeout socket readiness checks; physical confirmation remains under runtime concurrency Step 6. |

## Superseding Decision
The original “persist before mirroring” decision is retained above as history,
but it is no longer the runtime policy. Physical Switch evidence and commit
`ce0257b` show that per-record SD flushing can stall the logger and any caller
waiting on its sink lock. The active policy is one runtime sink owner, live
nxlink as primary, fully buffered SD as fallback, and no sink I/O under a mutex.
