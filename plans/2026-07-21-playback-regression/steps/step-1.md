# Step 1: Restore Shared Playback

> Status: IN_PROGRESS
> Created: 2026-07-21

## Goal
Identify the common load failure and implement the smallest regression fix that restores DLNA and IPTV while keeping AirPlay isolated.

## Prerequisites
- The latest physical-test logs are available under `logs/`.
- Existing AirPlay and player changes remain intact for comparison.
- Files to inspect: `source/player/`, `source/protocol/airplay/integration.c`, `source/main.c`, and protocol load callers.

## Deliverables
- Evidence identifying the last successful player state and first failing state.
- A focused code fix and regression coverage for the shared lifecycle issue.
- AirPlay capability and SETUP traces that identify the missing mirror-stream negotiation state.
- After this step: host/sanitizer tests and a strict traced Switch build pass.

## Plan
- [x] `bash`/`rg` `logs/` — select the latest run and summarize player, FFmpeg, ownership, AirPlay and protocol events in sequence.
- [x] `read`/`rg` `source/player/`, `source/main.c`, `source/protocol/{airplay,dlna}/` and `source/iptv/` — trace every load/stop/ownership caller and recent diff.
- [x] `edit` the evidenced shared lifecycle target and its closest host test — prevent stale AirPlay work from interrupting non-AirPlay playback.
- [x] `bash` host tests, ASan/UBSan, `git diff --check`, and strict traced Switch build — require all checks to pass.
- [x] `edit` libmpv AirPlay stream registration — defer the custom protocol until a mirror bridge is actually bound so DLNA/IPTV use the pre-AirPlay initialization path.
- [x] `edit` AirPlay `/info` and SETUP diagnostics — match the selected compatibility profile and expose timing/stream metadata without logging secrets.
- [x] `bash` rerun host, sanitizer and strict traced Switch validation after the follow-up changes.
- [x] `read` `logs/runtime.log`, `v0.2.0:source/main.c`, and `source/iptv/iptv.c` — verify the current process starts libmpv before FFmpeg's global network runtime, unlike the known-good release.
- [x] `edit` `source/main.c` and `source/iptv/iptv.c` — restore process-level FFmpeg network initialization before player/protocol threads and deinitialize it after player shutdown.
- [x] `bash` host tests, `git diff --check`, and a clean strict full-trace Switch build — verify the lifecycle fix without changing nvtegra/deko3d or protocol behavior.

## Quality Checklist
- [x] Evidence-before-edit: target read, impact search and validation commands recorded.
- [x] Existing pattern / reuse checked: existing ownership generations and player command queue are preferred.
- [x] Contract understood: one protocol owns playback; stale commands must not affect a newer owner.
- [x] Risk reviewed: concurrency, stale callback, network/player shutdown and hardware playback regressions.
- [x] Mitigation recorded: focused generation/lifecycle test plus sanitizers and strict target build.

## Validation Checklist
- [x] Relevant host tests exit 0.
- [x] `git diff --check` exits 0.
- [x] Strict traced Switch build exits 0.

## Test Checklist
- [x] Focused shared-player regression test passes.
- [x] ASan/UBSan host suite passes.

## Implementation Notes
The shared failure occurs before decoding: `MPV_EVENT_START_FILE` is received, but `MPV_EVENT_FILE_LOADED` is not. Restored the known-good immediate unpause and minimal per-file options rather than changing FFmpeg protocols or falling back to software decode. Separately, AirPlay mirror ownership now moves from SETUP/open to actual RECORD, so an incomplete AirPlay connection cannot stop DLNA/IPTV. The 15:49 physical retest proved those changes were not sufficient: normal HTTP still stalls before `FILE_LOADED`, while AirPlay now accepts RECORD but never receives a type-110 stream SETUP. The next focused change removes AirPlay's only shared libmpv-init hook until a mirror stream exists, aligns `/info` with the selected UxPlay compatibility profile, and records non-secret SETUP shape/timing evidence.

The follow-up now returns a live UDP NTP timing port and sends the legacy timing request sequence expected by the iPhone. `TRACE_MEDIA=1` also captures selected libmpv network/demux info without changing Release logging. Automated validation is complete; the step stays in progress until a physical Switch run confirms DLNA/IPTV `FILE_LOADED` and AirPlay's second stream SETUP.

The user identified `v0.2.0` as the stronger known-good baseline. The normal libmpv path has therefore been restored exactly to that release's direct-MP4 cache, deferred autoplay, and nvtegra/deko3d surface policy. The previous rollback to pre-`v0.2.0` behavior was not retained.

The release logger still defaults to `WARN`. A separate `TRACE_LOG=1` build now lowers the runtime threshold to `DEBUG`, while `TRACE_MEDIA=1` requests libmpv `info` messages and promotes the `stream`, `cache`, `demux`, and `ffmpeg` families. The dedicated VS Code launch deliberately omits `TRACE_AIRPLAY=1`, preventing verbose AirPlay traces from obscuring DLNA and player evidence.

The latest nxlink logs stop at startup with a peer reset before DLNA or IPTV requests arrive. Trace builds now mirror every queued line to `sdmc:/switch/NX-Cast/runtime_trace.log` before attempting stderr, and disable stderr mirroring after its first write failure. Full AirPlay traces are serialized through the same worker rather than writing to the duplicated nxlink socket from several protocol threads. This makes the SD file authoritative when the Mac-side session is truncated.

Historical comparison supersedes the two trace experiments above. The successful v0.2.0 run used the default WARN logger plus only `TRACE_MEDIA=1 TRACE_INPUT=1`; global DEBUG, mpv INFO promotion, and per-line SD flushing were absent. Those additions have therefore been removed. The new `NXCAST_AIRPLAY_RUNTIME=0` baseline task keeps current code linked but skips AirPlay startup, allowing one physical run to distinguish a shared player regression from AirPlay background interference without altering the release configuration.

The cross-protocol review found a real time-of-check/time-of-use race: ownership claim was atomic, but stopping the previous renderer and submitting the new URI happened outside that lock. A DLNA, IPTV or AirPlay thread could therefore lose ownership and still stop the new owner's stream. Media mutations now pass through one transition mutex, while discovery and request parsing remain parallel. A host concurrency test and ThreadSanitizer validate that a second owner cannot enter until the active transition completes; physical Switch playback remains the final check.

The persistent SD trace finally captured a complete failed DLNA-to-IPTV run. Both sources reach `MPV_EVENT_START_FILE`, but neither reaches `MPV_EVENT_FILE_LOADED`; the UI, main loop, logger and SOAP threads continue, and the IPTV URL still serves a valid redirected M3U8. Historical startup comparison found that the coordinator moved `iptv_init()` and its process-global `avformat_network_init()` from before libmpv initialization to after libmpv and its event thread. FFmpeg explicitly requires this optional compatibility initialization to run before threads that may use its TLS/network libraries. The initialization pair now belongs to the application lifecycle: start after the Switch socket stack but before player/protocol threads, and stop after player teardown. IPTV no longer owns or mutates the process-global FFmpeg network runtime.

The physical test rejected that hypothesis as a safe fix: the build stalled during startup and the pre-rendered home frame did not process X input. The process-global FFmpeg lifecycle experiment was removed in full, restoring the exact pre-experiment Full Trace NRO hash. The historical ordering difference remains diagnostic evidence only, not an accepted implementation.

The AirPlay-off A/B test is now decisive: with the same player actor and libmpv backend, IPTV reaches `FILE_LOADED` in 2.545 seconds and presents its first frame in 3.210 seconds. AirPlay-enabled runs stop producing heartbeats while AirPlay is still starting, so the shared player is not the root regression. AirPlay startup is now a single coordinator-owned worker instead of a coordinator worker spawning a second unmanaged worker. Its status snapshot uses a non-blocking mutex read, the startup worker runs below the main/player priority, completed startup threads are reaped immediately, and shutdown signals cancellation before joining. This establishes a fault boundary: a stalled AirPlay startup may remain `starting`, but it cannot block the main loop, logger, DLNA or IPTV. Handler identity strings are also validated with bounded scans so malformed startup data cannot run an unbounded `strlen` on the service worker.

## Files Changed
- `.vscode/tasks.json`
- `.vscode/launch.json`
- `source/main.c`
- `source/app/protocol_coordinator.c`
- `source/app/protocol_coordinator.h`
- `source/iptv/iptv.c`
- `source/log/log.c`
- `source/log/log.h`
- `source/player/backend/libmpv.c`
- `source/protocol/airplay/trace.h`
- `source/protocol/airplay/integration.c`
- `source/protocol/airplay/protocol/handlers.c`
- `source/protocol/airplay/protocol/handlers.h`
- `source/protocol/airplay/protocol/rtsp.c`
- `source/protocol/airplay/protocol/rtsp.h`
- `source/protocol/airplay/server.c`
- `source/protocol/airplay/media/mirror_runtime.c`
- `source/protocol/airplay/media/mirror_runtime.h`
- `source/protocol/airplay/mirror/timing.c`
- `source/protocol/airplay/mirror/timing.h`
- `makefile`
- `scripts/airplay_receiver_smoke_server.c`
- `scripts/test_airplay_handlers.c`
- `scripts/test_airplay_mirror_runtime.c`
- `scripts/test_airplay_timing.c`
- `scripts/test_protocol_coordinator.c`
- `plans/2026-07-21-playback-regression/plan.md`
- `plans/2026-07-21-playback-regression/steps/step-1.md`
