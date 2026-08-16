# Plan: Media Cache And Network Runtime Hardening

> Status: COMPLETE
> Created: 2026-08-13
> Last Updated: 2026-08-13

## Goal
Add a tested media-aware cache policy, promote the proven Switch network resource settings to production builds, and make cache/BSD/thread/socket pressure observable without perturbing playback.

## Assumptions
- The normal and release builds should use the Profile 13 resource policy because the user requested the architecture update after prior hardware experiments.
- AirPlay screen mirroring must remain low latency and must not inherit the network-media readahead policy.
- Existing uncommitted changes in `source/player/types.c`, `scripts/test_airplay_server_lifecycle.c`, and `scripts/test_log_mirror.c` are user work and must be preserved.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] DLNA, IPTV, and ordinary HTTP media use a forward 20 MiB/backward 10 MiB cache with a 20-second readahead ceiling.
- [x] `airplay://mirror` uses a distinct low-latency, non-readahead policy.
- [x] Normal and release builds initialize Switch networking with 12 BSD sessions, `sb_efficiency=8`, and exclusive media resource coordination.
- [x] Trace logs identify the selected cache policy, buffering transitions, configured BSD capacity, instrumented sockets/operations, and pressure/stall evidence without logging signed query strings.
- [x] Focused host tests and the available aggregate build/test targets pass.

### Non-goals
- Runtime UI controls for changing cache size.
- Increasing AirPlay RTSP client limits or introducing a generic HTTP thread pool.
- Treating instrumented socket count as the system socket descriptor limit.

### Edge Cases
- Empty/unknown/local URIs receive the stable network-media policy unless they are the explicit AirPlay mirror URI.
- Cache option formatting must fail safely on an undersized destination buffer.
- Diagnostic counters exclude libmpv/FFmpeg-internal sockets and must be labeled as instrumented values.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Network-media cache | 8 MiB existing direct-MP4 only; 20 MiB; 50/100 MiB | 20 MiB forward, 10 MiB backward, 20-second ceiling | yes |
| AirPlay mirror cache | Shared media cache; small cache; no demuxer cache | Low-latency no-cache policy | yes |
| Production network policy | libnx defaults; BSD8; BSD12/efficiency8/exclusive; BSD16 | BSD12/efficiency8/exclusive | yes |
| Concurrency structure | Generic thread pool rewrite; retain Actor/Supervisor/workers | Retain architecture and add budgets/observability | yes |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add and integrate a tested media-aware cache policy |
| Step 2 | `steps/step-2.md` | COMPLETED | Promote production network budgets and expose pressure diagnostics |
| Step 3 | `steps/step-3.md` | COMPLETED | Validate the integrated runtime and document the hardware test matrix |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Cache policy | `make test-player-cache-policy` | new focused host target | yes |
| Runtime diagnostics | `make test-runtime-diagnostics test-network-diagnostics` | existing host targets | yes |
| Coordinator behavior | `make test-protocol-coordinator` | existing host target | yes |
| Aggregate AirPlay/runtime suite | `make test-airplay` | existing CI-equivalent host target | yes when dependencies are available |
| Switch release build | `make release-build RELEASE_JOBS=4` | existing release target | yes when devkitPro dependencies are available |
| Patch hygiene | `git diff --check` | repository convention | yes |

## Context & Learnings
### Key Decisions
- Cache selection belongs at the libmpv load boundary because all DLNA/IPTV/AirPlay media commands converge there.
- Existing runtime/network diagnostics will be extended instead of adding a second monitoring subsystem.
- Resource logs will distinguish configured BSD service capacity from instrumented open sockets and operations.

### Gotchas & Warnings
- Wiliwili offers 0/10/20/50/100 MB on Switch but defaults to index 0, so its default is no explicit memory cache.
- NXMP defaults to 20 seconds of demuxer readahead, not a fixed 20 MB allocation.
- `demuxer-max-bytes` and `demuxer-max-back-bytes` are ceilings; they should not be described as memory allocated at startup.
- Existing Profile 1-12 semantics should remain diagnostic controls; production defaults should not silently rewrite those experiment definitions.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/player/backend/libmpv.c` | Shared media load boundary and cache observations | `read` found direct MP4-only 8/2 MiB options and DLNA-only cache sampling |
| `source/player/backend/libmpv_airplay.h` | Canonical mirror URI | `rg` found `PLAYER_LIBMPV_AIRPLAY_URI` as `airplay://mirror` |
| `source/app/network_diagnostics.[ch]` | Instrumented socket/operation registry | `read` found bounded active slots and per-subsystem snapshots |
| `source/app/runtime_diagnostics.[ch]` | Aggregate thread/socket resource snapshots | `read` found formatter and host-testable lifecycle counters |
| `source/main.c` | Switch socket initialization and trace heartbeat | `read` found compile-time BSD settings and runtime/network summaries |
| `Makefile` | Production/profile flags and test targets | `read` found release uses normal profile while BSD12/exclusive exists only in Profiles 13/14 |
| `../others/wiliwili-dev/wiliwili/source/utils/config_helper.cpp` | Wiliwili cache choices/default | `read` verified Switch choices and default index 0 |
| `../others/wiliwili-dev/wiliwili/source/view/mpv_core.cpp` | Wiliwili cache application | `read` verified N MiB forward and N/2 MiB backward ceilings |
| `../others/nxmp-master/source/iniparser/iniparser.h` | NXMP default cache window | `rg` verified default `demuxcachesec=20` |

### Verified Facts
- NX-Cast currently applies explicit `8MiB/2MiB/2s` cache settings only to direct MP4 URLs; other media inherits libmpv defaults — verified by `read source/player/backend/libmpv.c`, 2026-08-13.
- Wiliwili's Switch cache selector is 0/10/20/50/100 MB with default index 0; a nonzero N configures `demuxer-max-bytes=N MiB` and back bytes to N/2 — verified by local source reads, 2026-08-13.
- NXMP defaults demuxer readahead to 20 seconds and does not set a fixed byte ceiling in the inspected player initialization — verified by local `rg`, 2026-08-13.
- Profile 13/14 use BSD12, `sb_efficiency=8`, and exclusive media resources, while normal/release currently use libnx defaults — verified by `read Makefile` and `source/main.c`, 2026-08-13.
- Existing diagnostics count only explicitly instrumented NX-Cast sockets/operations, not libmpv/FFmpeg internals — verified by caller and API search, 2026-08-13.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-13 | Step 1 | Added and host-tested the 20/10 MiB network cache policy, low-latency AirPlay mirror policy, build tunables, and cache transition logs. |
| 2026-08-13 | Step 2 | Promoted BSD12/efficiency8/exclusive media settings to normal/release builds and added tested capacity/pressure diagnostics. |
| 2026-08-13 | Step 3 | Passed aggregate host and Switch release builds, documented log interpretation and the ordered physical test matrix. |
