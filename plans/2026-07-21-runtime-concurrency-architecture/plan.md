# Plan: Runtime Concurrency Architecture

> Status: ACTIVE
> Created: 2026-07-21
> Last Updated: 2026-07-21

<!--
  Plan-level status (lifecycle):
    DRAFT     — awaiting approval after clarification
    ACTIVE    — execution in progress
    COMPLETED — all steps done, verified
    ARCHIVED  — optional long-term archival state
  This is distinct from step-level status (PENDING|IN_PROGRESS|COMPLETED|BLOCKED)
  in `steps/step-N.md`. The pre-edit gate checks step status, not plan status.
-->

## Goal
Replace cross-thread direct player and service calls with explicit single-writer actors, bounded command queues, generation-based cancellation, and supervised lifecycle state machines so one protocol cannot freeze the UI or corrupt another protocol's playback.

## Assumptions
- The main thread remains the sole owner of controller/touch input and deko3d/ImGui foreground rendering.
- libmpv control and event processing can share one player worker, while libmpv render-context/frame APIs remain on the main thread behind a narrow render boundary.
- Protocol listeners may run concurrently, but they may not call player/backend APIs directly.
- Existing dirty AirPlay work must be preserved; migration will be incremental and compatibility adapters remain until each producer is moved.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] Main-loop input and rendering continue when any protocol start, stop, callback, or player command stalls.
- [x] Exactly one Media Actor thread executes player control commands and pumps backend events.
- [x] IPTV, DLNA, AirPlay, and local UI submit bounded commands carrying source identity and generation; stale commands cannot mutate a newer session.
- [x] Protocol lifecycle startup/shutdown is supervised asynchronously with observable per-service state, timeout, and failure reason.
- [x] Queue depth, rejected commands, command latency, actor heartbeat, and shutdown phase are available in runtime logs and snapshots.
- [x] Host tests cover ordering, takeover, stale command rejection, queue saturation, timeout, stalled service startup, and shutdown during load.
- [x] Strict nvtegra/deko3d Switch builds continue to pass without software-decoding fallback.

### Non-goals
- Rewriting protocol parsers, replacing libmpv/FFmpeg, or changing the deko3d rendering backend.
- Adding one thread per player command or attempting unsafe thread cancellation.
- Solving AirPlay protocol compatibility inside the concurrency migration.

### Edge Cases
- Queue full while a new SetMedia, Stop, seek, or volume command arrives.
- DLNA Play arrives after IPTV has taken ownership, or an AirPlay callback arrives after teardown.
- A protocol service stalls during startup while the user opens IPTV or exits the application.
- Shutdown starts while media is loading, a synchronous SOAP request is waiting, or an AirPlay stream reader is blocked.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Player concurrency | Shared mutexes vs actor/single writer | One Media Actor owns all player control and event pumping | yes — user requested design-first multithread management |
| Producer API | Direct calls vs command queue | Bounded command queue with deep-copied payload and explicit enqueue result | yes — follows the proven logger producer/consumer model |
| Session arbitration | Global transition lock vs source generation | Actor-owned active source plus monotonically increasing generation | yes — stale work must be rejected without holding locks across backend calls |
| UI semantics | Wait for command completion vs fire-and-observe | Main/UI submits asynchronously and observes snapshots/events | yes — input/render must never wait on media/network work |
| Protocol semantics | All async vs bounded acknowledgements | SOAP may wait with a bounded timeout; IPTV/AirPlay callbacks enqueue and return accepted/rejected | yes — preserves protocol responses without blocking the main thread |
| Service lifecycle | Synchronous startup in `main` vs supervised FSM | Supervisor advances service states outside the main/render thread | yes — a failed service must degrade independently |
| Queue overload | Unbounded growth vs drop everything vs explicit policy | Fixed capacity; reject critical commands, coalesce replaceable seek/volume updates, expose counters | yes — deterministic memory and failure behavior on Switch |
| Shutdown | Ad hoc joins vs dependency phases | Stop producers, cancel/drain media work, detach render, destroy backend, flush logger, then release network/platform | yes — prevents new work during teardown |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Publish the executable thread-ownership and message-flow contract without runtime changes. |
| Step 2 | `steps/step-2.md` | COMPLETED | Add a host-tested bounded media command queue and actor execution path behind the player API. |
| Step 3 | `steps/step-3.md` | COMPLETED | Migrate UI, IPTV, DLNA, and AirPlay to actor commands and remove direct renderer mutations. |
| Step 4 | `steps/step-4.md` | COMPLETED | Make protocol startup/status/shutdown a nonblocking supervised state machine. |
| Step 5 | `steps/step-5.md` | COMPLETED | Add health snapshots, watchdog diagnostics, fault injection, and deterministic shutdown. |
| Step 6 | `steps/step-6.md` | IN_PROGRESS | Run full host/Switch regression validation and define the physical-test promotion gate. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Thread boundary audit | `rg -n "renderer_|player_(set_media|play|pause|stop|seek)" source` | Existing direct-call inventory | yes |
| Focused actor tests | `make test-player-actor` | Added in Step 2 | yes |
| Coordinator tests | `make test-protocol-coordinator` | Existing Makefile target | yes |
| Full host regression | `make test-airplay` | Existing Makefile target | yes |
| Race checks | focused actor/coordinator tests with ThreadSanitizer where supported | Existing ownership TSAN pattern | yes |
| Formatting | `git diff --check` | Existing repository workflow | yes |
| Switch build | `make TRACE_MEDIA=1 TRACE_INPUT=1 TRACE_AIRPLAY=1 NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 -j4` | Existing strict Full Trace contract | yes |

## Context & Learnings
### Key Decisions
- Thread ownership replaces lock ownership: external callers never hold a player lock while libmpv, FFmpeg, network, filesystem, or callbacks execute.
- The existing player event thread is extended into the Media Actor instead of adding another competing player thread.
- The current public player calls remain as temporary compatibility adapters, then are removed from protocol/UI call sites as each vertical slice migrates.
### Gotchas & Warnings
- `mpv_render_context_*` remains main-thread-owned; Media Actor ownership applies to control/event APIs, not foreground rendering.
- libnx does not provide a safe general-purpose thread kill; service operations must be designed to return or own cancellable workers rather than relying on forced cancellation.
- The worktree is already dirty. Do not reset, stash, or commit unrelated AirPlay/player changes as part of this migration.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `docs/threading-design.md` | Existing intended threading contract | Claims a player owner thread already executes commands, which current code contradicts. |
| `source/player/core/session.c` | Existing player event thread and public command dispatch | `player_thread_main` only pumps events; command helpers execute backend functions on caller threads. |
| `source/player/renderer.h` | Current direct-call compatibility surface | Inline wrappers expose synchronous player mutation to every protocol/UI caller. |
| `source/app/protocol_coordinator.c` | Current lifecycle and ownership coordinator | `protocol_coordinator_start` synchronously invokes each service start before main loop. |
| `source/log/log.c` | Proven bounded producer/single-consumer pattern | Producers enqueue; one worker owns file/socket sinks and exposes drop counters. |
| `source/log/mirror.c` | Host-testable nxlink socket boundary | Zero-timeout poll plus nonblocking send converts peer backpressure into an observable drop. |
| `scripts/test_log_mirror.c` | Logger socket fault tests | Covers writable peer, saturated peer, repeated non-waiting writes, and disconnected peer. |
| `source/main.c` | Main-loop, startup, render, and shutdown composition | Calls synchronous coordinator start before controller/input loop begins. |
| `source/{iptv,protocol,player/ui}` | Playback command producers | Targeted `rg` shows direct renderer/player mutations from multiple execution contexts. |
| `source/log/mirror.c` | Physical logger follow-up | The mirror now relies on the already nonblocking socket and `send(MSG_DONTWAIT)`; it does not use a zero-timeout `poll()` preflight. |
| `source/main.c` | Known-good nxlink connection path | `nxlinkStdioForDebug()` duplicates the connected socket onto stderr as in v0.2.0; the logger mirrors through descriptor 2 while stdout remains local. |

### Verified Facts
- Current player commands execute the backend on the caller thread; the player worker only calls `pump_events` — verified in `source/player/core/session.c`, 2026-07-21.
- IPTV, DLNA SOAP, AirPlay callbacks, main, and UI controls all directly reach renderer/player mutation APIs — verified by scoped `rg`, 2026-07-21.
- `protocol_coordinator_start()` invokes IPTV, DLNA, and AirPlay start operations synchronously before the main input loop — verified in `source/app/protocol_coordinator.c` and `source/main.c`, 2026-07-21.
- `docs/threading-design.md` describes a player owner thread as the only playback executor, but that design is not implemented — verified by document/source comparison, 2026-07-21.
- The logger already demonstrates a bounded queue, single sink owner, nonblocking producers, drop accounting, and deterministic worker shutdown on Switch — verified in `source/log/log.c`, 2026-07-21.
- The latest failed experiment showed that moving another global operation onto the synchronous startup path can leave a pre-rendered home frame visible while no input is processed — verified by physical report and `logs/run_nxlink-20260721-200652.log`, 2026-07-21.
- Full Trace hardware runs with nxlink enabled stopped producing logs within seconds and then froze the home frame, while upload-only runs kept rendering; the logger health snapshot path waited on the same sink mutex held across SD/socket I/O — verified by `logs/run_nxlink-20260721-223028.log`, `logs/run_nxlink-20260721-223318.log`, `logs/run_nxlink-20260721-223444.log`, and source inspection, 2026-07-21.
- Commit `ce0257b` documents the same historical Switch filesystem-freeze class and disabled file I/O as mitigation; the new persistent logger had reintroduced line-buffered SD writes plus per-record `fflush()` before nxlink mirroring — verified by `git show ce0257b` and current `source/log/log.c`, 2026-07-21.
- Logger health snapshots now read memory-only state, nxlink writes require a zero-timeout writable poll, and live nxlink suppresses SD writes so neither socket backpressure nor SD latency can block main/render progress — verified by focused normal/ASan/UBSan/TSan mirror tests and strict Full Trace Switch build, 2026-07-21.
- Logger file/socket configuration now completes before worker startup; the worker is the sole runtime sink owner and no mutex is held across sink I/O — verified by sink caller audit, `g_logSinkMutex` removal, and strict Full Trace Switch build, 2026-07-21.
- nxlink now uses `nxlinkConnectToHost(false, false)` instead of stderr redirection, so the logger is the only writer to the trace socket and libc/libmpv output cannot contend for the same stream — verified against the installed libnx `nxlink.h` contract and caller audit, 2026-07-21.
- Two follow-up physical runs no longer froze the UI, but Full Trace output stopped after the first heartbeat while the application kept running; the host reported `Connection reset by peer` only when the application exited, so that message is teardown evidence rather than the cause of the earlier silence — verified by `logs/run_nxlink-20260721-230222.log`, `logs/run_nxlink-20260721-230344.log`, and the user's live observation, 2026-07-21.
- Periodic runtime heartbeats are intentionally compiled only when `NXCAST_INPUT_TRACE_VERBOSE` is enabled, so their absence in the normal build is expected; the Full Trace run must continue reporting them every two seconds — verified in `source/main.c`, 2026-07-21.
- A single hard result from the former zero-timeout `poll()`/send path permanently disabled nxlink mirroring without notifying the host. The mirror now uses only nonblocking `send()`, treats backpressure/resource-pressure errors as transient drops, retries after hard errors, and restores v0.2.0's libnx stderr duplication path — verified by source audit, focused mirror tests, ASan/UBSan, TSan, and strict Full Trace Switch build, 2026-07-21.
- Full Trace previously requested mpv `info` messages but downgraded every message above warn to the application's debug level, so demuxer/network progress disappeared behind the INFO threshold. `TRACE_MEDIA=1` now preserves mpv info as application INFO while normal builds retain the quiet mapping — verified by source audit and strict Full Trace Switch build, 2026-07-21.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-07-21 | Step 1 | Replaced the inaccurate threading description with an explicit actor/supervisor contract, resource ownership table, command/session envelope, bounded overload policy, lock prohibitions, render handshake, lifecycle FSM, health data and shutdown phases. Updated the player-layer target path and recorded the five current direct-call producer files. `git diff --check` passes. |
| 2026-07-21 | Step 2 | Added a portable bounded Media Actor and replaced the player event worker with it. Player control calls now execute only on the actor; payload lifetime, saturation, timeout, queued-stop rejection and concurrent stop are host-tested. Focused normal, ASan/UBSan, TSan, strict libmpv/deko3d Switch build, and `git diff --check` pass. |
| 2026-07-21 | Step 3 | Migrated every production playback producer to normalized source/lease commands. UI, IPTV and AirPlay are nonblocking; DLNA has bounded acknowledgement. Actor-side generation validation rejects stale callbacks, AirPlay bridge references are queue-owned, and release is FIFO-ordered after stop/unbind. Full host regression and sanitizer runs pass; direct mutation audit is empty. |
| 2026-07-21 | Step 4 | Replaced sequential service startup with independent fixed lifecycle workers. Delayed/failing start, degraded state, stop-during-start and repeated-stop tests pass under TSan; main-facing startup returns without waiting for service work. |
| 2026-07-21 | Step 5 | Added actor/service/logger health, queue coalescing, nonblocking nxlink mirror backpressure, dependency-ordered shutdown, and static/fault tests. Backend init/finalize now run on the actor; command dispatch waits for main-thread render activation, enforcing render-context-before-URL. |
| 2026-07-21 | Step 6 | Automated host suite, ASan/UBSan, TSan, formatting, direct-call audit, and strict nvtegra/deko3d/Ed25519 trace build pass. Physical Switch switching/exit/soak matrix remains the release gate. |
| 2026-07-21 | Step 6 hardware logger regression | Reproduced a logger-dependent home-frame stall, removed sink-I/O locks from main-thread health snapshots, bounded nxlink readiness checks, and changed SD persistence to fallback-only while live nxlink is active. Focused sanitizer tests and strict trace build pass; another physical run is required. |
| 2026-07-21 | Step 6 logger ownership follow-up | Moved logger worker startup after sink configuration and removed the sink mutex, enforcing the documented rule that no lock spans filesystem or socket I/O. |
| 2026-07-21 | Step 6 nxlink single-writer follow-up | Replaced `nxlinkStdioForDebug()` with an unredirected nxlink connection owned exclusively by the logger worker. |
| 2026-07-21 | Step 6 nxlink continuity follow-up | Corrected the physical diagnosis: the peer reset occurred only at exit. Removed the zero-timeout poll preflight, made transient/hard mirror failures non-terminal, and restored the known-good v0.2.0 `nxlinkStdioForDebug()` stderr descriptor path. Focused mirror tests, the full AirPlay host suite, ASan/UBSan, TSan, and strict Full Trace build pass; NRO SHA-256 is `68f783f22a0535f5720838f62c24088650b8453293b75f74a49fb459a88e4c6d`. |
| 2026-07-21 | Step 6 playback observability follow-up | Preserved mpv INFO events in media-trace builds so the next physical run exposes DNS/TCP/HTTP/demuxer progress after `MPV_EVENT_START_FILE`. Strict Full Trace build and focused logger tests pass; NRO SHA-256 is `5043617addd5bea1bbc2614e53ea605b4da70d32fdc5d91e8af3dbb9b9caf60b`. |
