# Plan: Shared Playback Regression

> Status: ACTIVE
> Created: 2026-07-21
> Last Updated: 2026-07-21

## Goal
Restore DLNA and IPTV playback without weakening the nvtegra/deko3d path, then advance AirPlay from a successful control handshake to an actual mirror-stream setup without interfering with the shared player lifecycle.

## Assumptions
- The latest Switch logs in `logs/` correspond to the build where DLNA, IPTV and AirPlay all fail.
- Hardware decoding remains required; software decoding is not an acceptable workaround.
- Existing uncommitted AirPlay work must be preserved while the regression is isolated.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [ ] DLNA and IPTV can progress from URI load to normal libmpv playback rather than remaining in LOADING.
- [x] AirPlay startup does not claim, stop, replace or cancel media owned by DLNA/IPTV.
- [x] AirPlay traces expose whether the sender supplies timing and mirror-stream metadata, and `/info` matches the selected UxPlay compatibility profile.
- [x] The fix preserves nvtegra/deko3d and passes host, sanitizer and strict Switch builds.

### Non-goals
- Implementing AirPlay 2 multi-room audio or HomeKit/modern encrypted control.
- Replacing FFmpeg, libmpv, nvtegra or deko3d.

### Edge Cases
- AirPlay startup and shutdown overlap with a DLNA or IPTV load.
- Stale player ownership generations or custom stream callbacks cancel a newly loaded URI.
- Returning home while the player is still loading.

## Design Decisions
None — no design-sensitive changes.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | IN_PROGRESS | Diagnose and repair the shared playback regression with focused coverage. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Playback evidence | `rg -n "ERROR|WARN|LOADING|loadfile|owner|stream" logs/<latest>` | Existing runtime logs | yes |
| Host tests | `make test-airplay` and relevant player/IPTV tests | Existing Makefile targets | yes |
| Sanitizers | `make test-airplay HOST_CFLAGS='... -fsanitize=address,undefined ...'` | Existing AirPlay validation | yes |
| Switch build | `make NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 TRACE_MEDIA=1 TRACE_AIRPLAY=1 TRACE_INPUT=1 -j4` | Existing strict build contract | yes |
| Media/DLNA trace build | `make TRACE_LOG=1 TRACE_MEDIA=1 NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 -j4` | Dedicated playback diagnosis contract | yes |
| Playback baseline trace | `make TRACE_MEDIA=1 TRACE_INPUT=1 NXCAST_AIRPLAY_RUNTIME=0 NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 -j4` | Supersedes the noisy `TRACE_LOG` diagnostic build and matches the v0.2.0 trace policy | yes |

## Context & Learnings
### Key Decisions
- Diagnose the common player boundary before changing protocol-specific handlers or FFmpeg options.
### Gotchas & Warnings
- AirPlay's custom `airplay://mirror` stream and ownership worker share libmpv with DLNA/IPTV even when no AirPlay media session exists.
- The worktree already contains in-progress AirPlay changes; do not reset or overwrite them.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/` | Runtime failure evidence | User reports latest physical test remains loading across protocols. |
| `source/player/` | Shared libmpv, ownership and render lifecycle | DLNA, IPTV and AirPlay converge on the same player layer. |
| `source/protocol/airplay/integration.c` | Potential startup/shutdown interference | AirPlay composition owns callbacks into the shared player. |
| `source/main.c` | Startup, update and shutdown ordering | All protocols and player lifecycle are composed here. |

### Verified Facts
- DLNA, IPTV and AirPlay use the same NX-Cast process and libmpv backend — verified by the existing repository architecture, 2026-07-21.
- AirPlay registration and integration were changed recently while DLNA/IPTV previously played — verified by the current dirty worktree and user test history, 2026-07-21.
- The latest run reaches `mpv-start-file` but never emits `mpv-file-loaded`; DLNA `Play` remains deferred at `waiting-file-loaded` for up to 46,318 ms — verified in `logs/run_nxlink-20260721-152747.log`, 2026-07-21.
- A run without a successful AirPlay mirror setup shows the same DLNA stall, so AirPlay ownership is not the sole cause of the shared load failure — verified in `logs/run_nxlink-20260721-134157.log`, 2026-07-21.
- Commit `a7e54ca` changed the known-good behavior from immediate unpause to waiting for `FILE_LOADED`, added a direct-MP4 cache profile, disabled lavc direct rendering and fixed lavc threads — verified by `git diff a7e54ca^ -- source/player/backend/libmpv.c`, 2026-07-21.
- AirPlay currently claims `PLAYER_MEDIA_OWNER_AIRPLAY_MIRROR` in the SETUP-time `mirror_open` callback, while the runtime only binds and loads `airplay://mirror` from the later RECORD callback — verified in `source/protocol/airplay/integration.c` and `source/protocol/airplay/media/mirror_runtime.c`, 2026-07-21.
- Validation targets are `make test-airplay`, the same target under ASan/UBSan, `git diff --check`, and the strict traced Switch build listed above — verified from the existing Makefile workflow, 2026-07-21.
- The iPhone completes FairPlay and the initial key SETUP, then sends RECORD before a stream SETUP; NX-Cast rejects it with 455 because `mirror_setup` is false — verified in `logs/run_nxlink-20260721-152747.log` and `source/protocol/airplay/protocol/handlers.c`, 2026-07-21.
- Current UxPlay accepts RECORD without enforcing prior mirror-stream initialization, so NX-Cast's stricter ordering is an interoperability difference — verified against the current `FDH2/UxPlay` `raop_handler_record`, 2026-07-21.
- The 15:49 retest still reaches `MPV_EVENT_START_FILE` and dispatches `pause=false`, but never reaches `MPV_EVENT_FILE_LOADED`; the autoplay/cache/lavc rollback was therefore insufficient — verified in `logs/run_nxlink-20260721-154941.log`, 2026-07-21.
- The same retest completes pair-verify, both FairPlay stages, initial SETUP and RECORD with status 200, but never sends a second SETUP containing `streams`/type 110; no mirror listener, ownership claim or `airplay://mirror` load can occur — verified in `logs/run_nxlink-20260721-154941.log`, 2026-07-21.
- The latest log opens and scrolls the IPTV panel but contains no IPTV channel `set_media`, so it does not independently demonstrate the IPTV load failure — verified in `logs/run_nxlink-20260721-154941.log`, 2026-07-21.
- Registering `airplay://` through `mpv_stream_cb_add_ro()` before `mpv_initialize()` is the only AirPlay-specific change inside the otherwise shared libmpv initialization path — verified by `git diff 11ff3e9 -- source/player/backend/libmpv.c`, 2026-07-21.
- The AirPlay mirror ring buffer is allocated only from `airplay_mirror_runtime_open()` after a type-110 stream SETUP, not during application startup — verified in `source/protocol/airplay/media/mirror_runtime.c`, 2026-07-21.
- NX-Cast's full `/info` response omits UxPlay's audio format/latency and keepalive fields, reports `refreshRate` as `60.0` instead of the legacy receiver interval, and currently returns timing port zero — verified against the audited local UxPlay reference and `source/protocol/airplay/protocol/handlers.c`, 2026-07-21.
- The known-good 2026-07-16 DLNA run reaches `FILE_LOADED` immediately after the same `START_FILE`/Play sequence; the current 2026-07-21 run stops at `START_FILE`, before decoder initialization — verified by comparing `logs/run_nxlink-20260716-210113.log` and `logs/run_nxlink-20260721-154941.log`, 2026-07-21.
- AirPlay's legacy initial SETUP advertises NTP timing and requires NX-Cast to return a live UDP timing port; a host UDP round-trip now verifies the timing request/response path — verified by `scripts/test_airplay_timing.c` and `make test-airplay`, 2026-07-21.
- Tag `v0.2.0` resolves to commit `11ff3e9` and is the user-confirmed playback baseline; its normal libmpv path includes the direct-MP4 cache profile, deferred unpause at `FILE_LOADED`, `vd-lavc-dr=no`, and four lavc threads — verified with `git show v0.2.0:source/player/backend/libmpv.c`, 2026-07-21.
- After restoring that baseline, the remaining `v0.2.0..current` libmpv diff is limited to the AirPlay stream bridge, trace-only log promotion, AirPlay bridge cleanup, and the unavailable-backend stub — verified with `git diff v0.2.0 -- source/player/backend/libmpv.c`, 2026-07-21.
- The global logger defaults to `WARN`, while HTTP, SSDP, SOAP and SCPD success paths primarily log at `DEBUG`/`INFO`; the old all-protocol trace task enabled AirPlay's direct trace without lowering the global threshold — verified in `source/log/log.c`, DLNA call sites, and `.vscode/tasks.json`, 2026-07-21.
- A clean `TRACE_LOG=1 TRACE_MEDIA=1` build contains HTTP/SOAP/SSDP/SCPD/media/libmpv log markers and contains no AirPlay verbose trace marker — verified with `strings NX-Cast.nro`, 2026-07-21.
- The 16:37 and 16:39 nxlink sessions both terminate with `Connection reset by peer` before any DLNA/IPTV media request is recorded; the shorter logs therefore cannot diagnose playback and do not prove FFmpeg was reached — verified in `logs/run_nxlink-20260721-163713.log` and `logs/run_nxlink-20260721-163905.log`, 2026-07-21.
- libnx duplicates the nxlink socket onto stderr and returns the original socket descriptor, so concurrent direct `stderr` writers bypass the NX-Cast logger while sharing the same underlying connection — verified by disassembling the installed official `libnx.a` `nxlink_stdio.o`, 2026-07-21.
- Trace builds now persist the asynchronous log to `sdmc:/switch/NX-Cast/runtime_trace.log`; Switch AirPlay trace calls use that logger instead of writing directly to stderr from protocol threads — verified in `source/log/log.c`, `source/main.c`, and `source/protocol/airplay/trace.h`, 2026-07-21.
- The user-confirmed v0.2.0 trace did not enable global DEBUG or SD runtime logging; it kept the logger at WARN and promoted only `TRACE_MEDIA`/`TRACE_INPUT` events, producing the complete SetAVTransportURI-to-playback chain without startup noise — verified in tag `v0.2.0`, `.vscode/tasks.json`, and `logs/run_nxlink-20260716-210113.log`, 2026-07-21.
- The logger has been restored byte-for-byte to v0.2.0, and the speculative `TRACE_LOG`/`runtime_trace.log` path has been removed because per-line SD flushing and global DEBUG were not part of the known-good playback design — verified with `git diff --quiet v0.2.0 -- source/log/log.c source/log/log.h`, 2026-07-21.
- Current media trace coverage still contains the same 24 `player_trace_*` call sites as v0.2.0; no key playback transition was lost — verified with current and tagged source searches, 2026-07-21.
- The trace-only promotion of mpv `cplayer`, `stream`, `cache`, `demux`, and `ffmpeg` INFO messages was removed, so playback traces now emit only the historical state chain plus real WARN/ERROR records — verified in `source/player/backend/libmpv.c`, 2026-07-21.
- `NXCAST_AIRPLAY_RUNTIME=0` now leaves the AirPlay implementation linked but skips its startup thread, RTSP listener and mDNS worker, providing a controlled v0.2.0-style runtime baseline for DLNA/IPTV physical testing — verified by the strict Switch build and the NRO marker, 2026-07-21.
- Player ownership claims were serialized, but each protocol stopped or modified the renderer after releasing the ownership mutex. A second protocol could therefore claim the player before the first protocol's stale `renderer_stop()` executed, allowing the old thread to stop the new stream — verified across DLNA SetAVTransportURI, IPTV channel playback and AirPlay claim/RECORD paths, 2026-07-21.
- A dedicated media-transition mutex now serializes ownership claim plus renderer stop/load/control submission across DLNA, IPTV, AirPlay URL video and AirPlay mirroring; the ordinary request, mDNS and HTTP threads remain concurrent — verified in `source/player/core/ownership.c` and all claim/control call sites, 2026-07-21.
- The ownership test now proves a competing protocol cannot claim while another media transition is active. The complete host suite, ThreadSanitizer ownership test, strict full-trace Switch build and `git diff --check` pass — verified locally, 2026-07-21.
- The latest persistent trace proves both a valid DLNA HTTP MP4 and a valid IPTV HLS URL reach `MPV_EVENT_START_FILE` but never reach `MPV_EVENT_FILE_LOADED`, while main/UI/SOAP/logger heartbeats continue; the failure is below protocol dispatch and above decoding — verified in `logs/runtime.log`, 2026-07-21.
- `v0.2.0` calls `iptv_init()` and therefore `avformat_network_init()` before `player_init()`, while the current coordinator starts IPTV after libmpv and its event thread. FFmpeg documents that this global initialization, when used, must occur before threads using its TLS/network libraries start — verified in `v0.2.0:source/main.c`, current `source/main.c`, `source/iptv/iptv.c`, and the installed `libavformat/avformat.h`, 2026-07-21.
- The IPTV HLS URL from the failing trace still returns a valid redirected M3U8 manifest, so this physical run is not explained by a dead channel entry — verified with a bounded HTTP fetch from the development host, 2026-07-21.
- `Connection reset by peer` is emitted when the user closes NX-Cast, not when logging first stops; it must not be used as the crash or failure timestamp — corrected by the physical tester, 2026-07-22.
- The controlled AirPlay-off build keeps the current player actor/libmpv implementation and successfully reaches IPTV `FILE_LOADED` at 2.545 seconds and the first rendered frame at 3.210 seconds, while its logger heartbeat remains healthy — verified in `logs/run_nxlink-20260722-000659.log`, 2026-07-22.
- The AirPlay-enabled run stops producing application logs while the AirPlay startup worker is active, whereas the AirPlay-off run plays normally; the regression boundary is therefore AirPlay runtime startup rather than FFmpeg, nvtegra, deko3d, DLNA or IPTV — verified by the user A/B test and `logs/run_nxlink-20260722-000553.log`, 2026-07-22.
- AirPlay startup is now owned by one lower-priority protocol-coordinator worker. Status reads are non-blocking, completed startup workers are reaped, and shutdown requests cancellation before joining; a stalled AirPlay service cannot synchronously block the main loop — verified by `scripts/test_protocol_coordinator.c`, full `make test-airplay`, and the clean Full Trace Switch build, 2026-07-22.

### Risk Review
- Concurrency risk: an incomplete AirPlay SETUP can preempt another protocol; mitigation is to claim ownership immediately before RECORD enqueues the mirror load.
- Playback risk: per-file cache flags and delayed unpause can interact differently with the Switch libmpv port; mitigation is to restore the previously working minimal `pause` option and immediate asynchronous unpause.
- Hardware risk: disabling lavc direct rendering changes the nvtegra/deko3d path; mitigation is to remove the speculative override and retain `hwdec=nvtegra` plus the existing deko3d render context.
- Protocol-order risk: accepting RECORD before stream SETUP must not touch the player early; mitigation is to record intent only and invoke the mirror callback once after both conditions are true.
- Worktree risk: unrelated in-progress AirPlay changes are already dirty, so a pre-edit WIP commit would capture unrelated work; scope is preserved with focused diffs and this persisted plan instead.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-07-21 | Step 1 | Restored immediate libmpv unpause and minimal load options, removed speculative lavc overrides, delayed AirPlay ownership until RECORD, and accepted RECORD-before-stream-SETUP ordering. Automated host, sanitizer and strict Switch validation passed; physical playback retest remains. |
| 2026-07-21 | Step 1 | Physical retest disproved the initial libmpv option hypothesis. Follow-up scope isolates AirPlay's custom protocol registration from normal HTTP/HLS playback and adds protocol-compatible `/info` plus SETUP diagnostics. |
| 2026-07-21 | Step 1 | Deferred `airplay://` registration until mirror binding, aligned legacy `/info`, added live UDP NTP timing, and promoted selected mpv network/demux info only in `TRACE_MEDIA` builds. Host and sanitizer suites pass; strict Switch build passed before the final trace-only logging adjustment. |
| 2026-07-21 | Step 1 | Rebased the ordinary libmpv load/autoplay/decode policy on the user-confirmed `v0.2.0` release rather than the older pre-release behavior, while retaining on-demand AirPlay registration. |
| 2026-07-21 | Step 1 | Post-baseline host suite, strict traced Switch build, and `git diff --check` passed; generated NRO SHA-256 is `b85d0d49039ca9196ff127a48b2dd09238d6b4cebca8907ea8d689b36be79690`. |
| 2026-07-21 | Step 1 | Added an explicit `TRACE_LOG=1` global debug threshold and a dedicated Media/DLNA VS Code trace launch that leaves AirPlay trace disabled, so HTTP/SOAP/SSDP/libmpv/FFmpeg evidence is visible without AirPlay noise. |
| 2026-07-21 | Step 1 | Clean Media/DLNA trace build and the full host test suite passed; generated NRO SHA-256 is `7c5c5be47a1c6f3ca907b8240d14fb2400840c51a415fcf50ba77023d3ad0794`. |
| 2026-07-21 | Step 1 | Latest nxlink sessions were shown to lose the diagnostic socket before playback requests. Added an SD-persistent trace sink, disabled failed stderr mirroring, serialized Switch AirPlay traces through the logger, and clarified the full-trace VS Code launch. Media and full strict builds plus the host suite pass; full-trace NRO SHA-256 is `8db76b63d01de98077d7ce94d12ed00b5d48cdef8d713600a44a601df54865ea`. |
| 2026-07-21 | Step 1 | Historical comparison superseded the global DEBUG/SD trace experiment: restored the v0.2.0 WARN logger, removed mpv INFO promotion, and added a Playback Baseline Trace launch that disables only the AirPlay runtime. Strict baseline build, JSON/task validation, `git diff --check`, and the full AirPlay/ownership host suite pass; baseline NRO SHA-256 is `3eee2a3e6ba75e105f31b0e8f337ad607ec6650a68bbe706eb03572b3dc4e0e0`. |
| 2026-07-21 | Step 1 | Fixed a cross-protocol TOCTOU race by adding a shared media-transition mutex around ownership claim and renderer mutations for DLNA, IPTV and both AirPlay paths. Added a blocking concurrency assertion to the ownership test; the host suite, ThreadSanitizer test, strict full-trace Switch build and diff checks pass. Full-trace NRO SHA-256 is `5742e68722c66c3e872548088e0ee12d4d1590f1a278785f94eaba8c7575aa7f`. |
| 2026-07-21 | Step 1 | Latest persistent trace identified a process-global FFmpeg network initialization order regression. Moved that lifetime out of IPTV and ahead of player/protocol thread creation, with deinit after player shutdown. Full host suite, clean strict Full Trace Switch build and diff checks pass; NRO SHA-256 is `a2d15630bd2b5620219562b67d6c3f2e2759201b6648e4e241487a4db6bd9c89`. Physical `FILE_LOADED` confirmation remains. |
| 2026-07-21 | Step 1 | Physical test showed the FFmpeg network-order experiment introduced a startup/UI freeze and did not establish playback recovery. Reverted that experiment completely; the clean Full Trace NRO returned exactly to the pre-experiment SHA-256 `a9c4448b71b93bd184d5e0b93151e2f44c5e5039c643f15f821d861caf7cedd7`. Future fixes must not move blocking/global initialization onto the main startup path without a controlled baseline. |
| 2026-07-22 | Step 1 | AirPlay-off A/B proved the shared player is healthy. Replaced AirPlay's nested async startup with one coordinator-owned lower-priority worker, made status snapshots non-blocking, reaped completed startup threads, added cancellation-before-join, and bounded handler identity scans. Full host suite and clean Full Trace Switch build pass; NRO SHA-256 is `4cbb3ea94634083b68cc23a90f05df3d1391fbc5906c657b4fadb54eef39add2`. |
