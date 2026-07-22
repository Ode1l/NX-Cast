# Plan: Profile 14 Runtime Observability

> Status: COMPLETED
> Created: 2026-07-22
> Last Updated: 2026-07-22

## Goal
Add a Profile 14 that behaves exactly like Profile 13 while producing bounded diagnostics for AirPlay negotiation/thread/video failures, DLNA/libmpv buffering, and process resource ownership transitions.

## Assumptions
- Profile 14 uses the same BSD session, socket-buffer efficiency, receiver ownership, and playback behavior macros as Profile 13.
- HTTP status and Range details are best-effort observations from libmpv/FFmpeg log messages; an unavailable value is reported as `unknown`.
- Process-owned thread counters plus Horizon free-thread slots are the safe substitute for a privileged global thread enumeration.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] `full-owner-exclusive-observe-bsd12` is selectable and reports `profile_id=14` while preserving Profile 13 runtime behavior.
- [x] AirPlay logs negotiated `ct/spf/sr`, precise setup failure stages, and lifecycle counters for discovery/control/timing/audio/mirror/runtime threads.
- [x] AirPlay mirror diagnostics distinguish connection, configuration, decrypt, access-unit, bridge, and libmpv video stages without per-packet log spam.
- [x] DLNA loading/buffering/seeking emits at most one cache/video/HTTP/Range diagnostic sample per second.
- [x] Claim, stop, end-file, and next-load boundaries emit memory, heap, app-thread, free-thread-slot, and owned-socket snapshots.
- [x] Focused host tests and a Profile 14 Switch build pass.

### Non-goals
- Add or change AirPlay audio codec/sample-rate support.
- Change FFmpeg/libmpv options, network ownership policy, discovery suspension, scheduling, or playback behavior.
- Enumerate system-wide Horizon threads with privileged APIs.

### Edge Cases
- Unsupported Horizon firmware reports free thread slots as `unknown`.
- Missing libmpv properties or HTTP log metadata remain `unknown` and never fail playback.
- Failed thread creation is counted without decrementing the live count.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Profile behavior | Change resource policy vs inherit Profile 13 | Inherit Profile 13 exactly and add observation only | yes — user requested implementation after review |
| Log granularity | Per-packet vs periodic/event aggregates | One-second active-player aggregates plus boundary/failure events | yes — user requested shorter heartbeats and pure diagnostics |
| Thread accounting | Privileged global enumeration vs app registry | App-owned lifecycle registry plus free-thread slots | yes — reviewed before implementation |
| Audio scope | Add formats now vs diagnose first | Diagnose current AAC-LC/AAC-ELD path; no format change | yes — reviewed before implementation |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add Profile 14, reusable runtime counters/resource snapshots, and focused tests. |
| Step 2 | `steps/step-2.md` | COMPLETED | Instrument AirPlay setup, thread lifecycle, and mirror video pipeline with bounded aggregates. |
| Step 3 | `steps/step-3.md` | COMPLETED | Add libmpv/DLNA cache sampling, boundary snapshots, documentation, and full validation. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Runtime diagnostics test | `make TOPDIR=$PWD THIS_MAKEFILE=$PWD/makefile test-runtime-diagnostics` | Existing host-test Makefile pattern | yes |
| AirPlay regression suite | `make TOPDIR=$PWD THIS_MAKEFILE=$PWD/makefile test-airplay` | `makefile` target | yes |
| Switch build | `make TOPDIR=/f/VSCODE~1/NX-Cast THIS_MAKEFILE=/f/VSCODE~1/NX-Cast/makefile NXCAST_DIAG_PROFILE=full-owner-exclusive-observe-bsd12 NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 -j4` | Existing Profile 13 build command, new profile substituted | yes |
| JSON parse | `Get-Content -Raw .vscode/tasks.json \| ConvertFrom-Json \| Out-Null` | VS Code task configuration | yes |

## Context & Learnings
### Key Decisions
- Profile 14 is a measurement instrument, not another resource-management experiment.
- Sensitive URLs and payloads are excluded; counters and state labels are sufficient for diagnosis.
### Gotchas & Warnings
- The worktree contains unrelated user changes and CRLF-heavy diffs; edits must remain narrow and must not normalize files.
- `demuxer-cache-state` returned by synchronous `mpv_get_property(..., MPV_FORMAT_NODE, ...)` owns nested allocations that must be released immediately with `mpv_free_node_contents`.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `makefile` | Diagnostic profile and host-test targets | `rg NXCAST_DIAG_PROFILE` on 2026-07-22 |
| `.vscode/tasks.json` | User-selectable test profile | profile picker inspected on 2026-07-22 |
| `source/app/network_diagnostics.*` | Existing atomic socket/operation counters | source search on 2026-07-22 |
| `source/protocol/airplay/**` | Setup, service threads, mirror decrypt/video/bridge stages | source search on 2026-07-22 |
| `source/player/backend/libmpv.c` | Player properties, events, and FFmpeg log messages | source inspection on 2026-07-22 |
| `source/main.c` / `source/app/protocol_coordinator.c` | Runtime heartbeat and owner claims | source inspection on 2026-07-22 |
| `source/app/runtime_diagnostics.*` | Atomic AirPlay thread registry and bounded process/network snapshot formatter | focused host tests on 2026-07-22 |
| `scripts/test_runtime_diagnostics.c` | Lifecycle, failure, invalid-input, socket aggregation, and formatting tests | `test-runtime-diagnostics` passed on 2026-07-22 |
| `source/protocol/airplay/diagnostics.h` | Profile-gated AirPlay lifecycle/socket logging and per-packet suppression | host lifecycle logs and Profile 14 build on 2026-07-22 |
| `source/protocol/airplay/mirror/{video,mirror_session}.*` | Mirror config/decrypt/AU aggregate counters | focused test contracts and Switch build on 2026-07-22 |
| `source/protocol/airplay/media/{stream_bridge,mirror_runtime}.*` | Bridge byte/failure aggregates and one-second emission gate | Switch build on 2026-07-22 |
### Verified Facts
- Profile 13 is `full-owner-exclusive-bsd12` with diagnostic ID 13 — verified by `makefile`, 2026-07-22.
- libmpv already observes cache pause/seeking and handles log-message events — verified by `source/player/backend/libmpv.c`, 2026-07-22.
- Horizon exposes process memory and `InfoType_FreeThreadCount`; global thread listing is privileged — verified from installed libnx headers, 2026-07-22.
- AirPlay mirror video already follows TCP receive → AES decrypt → AVCC/Annex-B → MPEG-TS bridge → libmpv — verified by AirPlay mirror sources, 2026-07-22.
- Profile 14 CFLAGS match Profile 13 for BSD sessions, buffer efficiency, DLNA timeout, and exclusive ownership; its only additional behavior-independent macro is `NXCAST_RUNTIME_OBSERVABILITY=1` — verified by Make variable output, 2026-07-22.
- Runtime and network diagnostics focused host tests pass with strict `-Werror -pedantic` flags — verified by Make targets, 2026-07-22.
- Server, mDNS, and timing lifecycle tests pass with Profile 14 instrumentation enabled — verified by focused host commands, 2026-07-22.
- The complete AirPlay diagnostic changes compile and link into a Profile 14 NRO — verified by devkitA64 build, 2026-07-22.
- Full Cygwin host-suite execution is currently unavailable: it first stops at the unrelated `player/types.c` feature-macro issue, and host mbedTLS/FFmpeg packages are absent — verified by Make output and package/header probes, 2026-07-22.
- DLNA observation is gated by current DLNA ownership plus loading/buffering/seeking state and a one-second monotonic interval; unavailable libmpv fields print `unknown` and URLs are never emitted — verified by source review and clean Profile 14 build, 2026-07-22.
- Resource snapshots are emitted at claim, next-loadfile, Stop, END_FILE, and replaced-END_FILE boundaries without executing in Profile 13 — verified by compile gates and source review, 2026-07-22.
- Final clean NRO is 25,600,698 bytes with SHA-256 `7DC90E21B382011AF824376906B124C6AFC4543B3B2B61277368FD228CB1B2B5` — verified after clean devkitA64 build, 2026-07-22.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-07-22 | Step 1 | Added Profile 14 plumbing, AirPlay network categories, atomic thread/resource snapshots, and passing focused tests. |
| 2026-07-22 | Step 2 | Added Profile-gated AirPlay negotiation/failure/thread/video diagnostics; focused lifecycle tests and Switch build pass. |
| 2026-07-22 | Step 3 | Added 1 Hz DLNA/libmpv sampling, owner/player resource boundary snapshots, Profile 14 test documentation, and a passing clean Switch build. |
| 2026-07-22 | Reflection | Explicitly initialized every dynamic mirror-video atomic counter and enforced the one-second bridge-log gate even during repeated push failures; reran affected validation. |
