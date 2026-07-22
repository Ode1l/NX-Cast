# Plan: AirPlay Audio-First Negotiation and DLNA Remote Exit

> Status: COMPLETED
> Created: 2026-07-22
> Last Updated: 2026-07-22

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
Make Profile 13 accept iPhone audio-first AirPlay mirroring setup and reliably return the Switch to home when an established DLNA controller exits without sending `Stop`.

## Assumptions
- Physical verification will continue with `NXCAST_DIAG_PROFILE=full-owner-exclusive-bsd12` on the same Switch and phone used for the 2026-07-22 log.
- A DLNA controller that never establishes a recurring polling pattern must retain standard renderer behavior and must not be stopped by inactivity.
- AirPlay audio received before the mirror stream may be discarded until the shared stream bridge and recording state are ready.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] Initial AirPlay `SETUP`, deferred `RECORD`, audio-only `SETUP`, and later mirror `SETUP` are accepted by the handler/runtime implementation and protected by a regression case.
- [x] Audio-first AirPlay runtime state is cleaned on `TEARDOWN` even if no mirror stream ever arrives.
- [x] A normal DLNA `Stop` still stops/releases immediately and now queues main-loop home restoration.
- [x] In Profile 13 only, four recurring transport/position queries arm controller tracking and ten seconds without another query requests player stop and home restoration.
- [x] DLNA sessions without an established polling pattern are never stopped by the inactivity policy.
- [x] Runnable focused host tests and the clean Profile 13 Switch build pass; the dependency-blocked AirPlay host suite is covered by target compilation plus physical-test handoff.

### Non-goals
- Redesigning the home/player rendering model, changing libmpv/FFmpeg options, or inferring DLNA exit from TCP connection closure.
- Enabling the DLNA inactivity policy in the normal release profile before physical validation.
- Solving video codec errors unrelated to AirPlay protocol negotiation.

### Edge Cases
- AirPlay audio-only setup followed directly by `TEARDOWN`; mirror setup failure after audio setup; repeated setup attempts; stop racing an in-progress setup.
- DLNA explicit `Stop`, natural media end, paused playback, controller queries that are too sparse to establish a session, and a new URI after a prior timeout.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| AirPlay setup ordering | Require mirror first; create a standalone audio bridge; hold audio receiver/format until mirror bridge exists | Accept audio first, retain its decoded format, then configure the single shared bridge when mirror setup arrives | yes — user authorized the AirPlay no-video fix |
| DLNA exit signal | Require SOAP `Stop`; treat every socket close as exit; use an established-polling inactivity policy | Preserve immediate `Stop` and add a conservative 4-query/10-second inactivity path | yes — user clarified phone exit without Switch exit is the bug and authorized the fix |
| Rollout scope | Enable globally; add a new profile; gate in current diagnostic profile | Gate the heuristic in existing Profile 13 until physical behavior is verified | yes — current work and requested testing target Profile 13 |
| Stop execution boundary | Stop synchronously in the SOAP thread; add another watchdog thread; reuse the main-loop home transition | Keep tracking lock-free and let the main loop call the existing asynchronous return-home path | yes — avoids a new network/resource thread and preserves existing UI/player coordination |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Support and protect AirPlay audio-first setup and cleanup |
| Step 2 | `steps/step-2.md` | COMPLETED | Detect an established DLNA controller disappearing and reuse return-home |
| Step 3 | `steps/step-3.md` | COMPLETED | Run integrated regression checks and produce the Profile 13 test build |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Focused AirPlay tests | `make test-airplay` | `makefile:598-638` compiles and runs handler/runtime tests inside the AirPlay suite | yes when host mbedTLS/libsodium/FFmpeg are available |
| DLNA controller-session test | `make test-dlna-controller-session` | Added in Step 2 following existing host-test targets near `makefile:505` | yes |
| Coordinator regression | `make test-protocol-coordinator` | `makefile:505-508` | yes |
| Shutdown regression | `make test-shutdown-order` | `makefile:588-589` | yes |
| Switch Profile 13 build | `make clean && make TOPDIR="$PWD" THIS_MAKEFILE="$PWD/makefile" PORTLIBS_PREFIX=/opt/devkitpro/portlibs/switch NXCAST_DIAG_PROFILE=full-owner-exclusive-bsd12 NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 -j4` | Existing Profile 13 flags at `makefile:209-215` and verified local devkitPro environment | yes |

## Context & Learnings
### Key Decisions
- Controller inactivity is policy, not a UPnP termination signal; it remains diagnostic-profile-only until hardware evidence supports promotion.
- The main loop already owns asynchronous player stop and home restoration, so the DLNA control layer only records controller liveness.
- AirPlay audio and video continue to share one bridge; audio-first support changes construction order, not playback ownership.
### Gotchas & Warnings
- TCP request connection closure is not a DLNA session boundary because SOAP queries use short-lived HTTP connections.
- The AirPlay runtime must destroy timing/audio objects even when `session_id` never became a mirror media session.
- Existing user changes in the dirty worktree must be preserved; only task-path hunks may be edited.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260722-193121.log` | Physical protocol trace | `read`/`rg` found successful explicit `AVTransport Stop`, failed exits with polling cessation only, and AirPlay audio-only SETUP returning 461 |
| `source/protocol/airplay/protocol/handlers.c` | RTSP SETUP ordering | `read` found audio type 96 rejected by `!context->mirror_setup` in `setup_streams()` |
| `source/protocol/airplay/media/mirror_runtime.c` | Audio/video runtime ownership | `read` found `audio_open` requires an existing bridge and `stop` skips audio destruction when no mirror media exists |
| `scripts/test_airplay_handlers.c` | Handler contract tests | `rg`/`read` found existing initial SETUP, deferred RECORD, combined stream SETUP, and TEARDOWN coverage |
| `scripts/test_airplay_mirror_runtime.c` | Runtime lifecycle tests | `rg`/`read` found existing open/record/keyframe/stop cycles and fake player operations |
| `source/protocol/dlna/control/action/avtransport.c` | SOAP actions and controller polls | `read` found immediate owned stop/release plus `GetTransportInfo` and `GetPositionInfo` entry points |
| `source/main.c` | Main-loop stop/home policy | `read` found `main_request_player_home()` and `protocol_coordinator_tick()` integration |
| `source/app/protocol_coordinator.c` | Ownership release behavior | `rg` found terminal playback observation releases the current lease and returns desired resources to home |
| `makefile` | Profiles and host/Switch validation | `rg` found Profile 13 flags and current AirPlay/coordinator test commands |
| `source/protocol/dlna/control/controller_session.c` | Cross-thread DLNA control liveness/event state | Focused host test covers arming, refresh, timeout, eligibility reset, explicit stop event, and retries |
| `scripts/test_dlna_controller_session.c` | Deterministic controller-session contract | `make test-dlna-controller-session` repeatedly exits 0 under strict C11 warnings |

### Verified Facts
- Profile 13 DLNA playback owns `dlna-exclusive`; explicit SOAP `Stop` stops the player and releases generation 6 immediately — verified by `read` of `logs/run_nxlink-20260722-193121.log`, 2026-07-22.
- Failed mobile exits in the captured run contain no `Stop`, `UNSUBSCRIBE`, or empty URI; the controller simply ceases `GetPositionInfo`/`GetTransportInfo` polling — verified by `rg`/`read` of the physical log, 2026-07-22.
- AirPlay sessions issue initial SETUP, deferred RECORD, then audio-only type 96 SETUP; current handler returns 461 before any media owner claim or mirror runtime log — verified by `rg`/`read` of the physical log and `handlers.c`, 2026-07-22.
- Existing `main_request_player_home()` sends asynchronous `STOP_ANY`, waits for terminal player state, and then switches to home — verified by `read` of `source/main.c:172-255`, 2026-07-22.
- No existing DLNA controller-liveness/watchdog abstraction was found — verified by `rg "watchdog|last_.*activity|poll.*count|deadline|expired" source scripts`, 2026-07-22.
- Existing AirPlay handler and mirror runtime host tests can be extended without a new AirPlay test harness — verified by `rg` of `scripts/test_airplay_handlers.c`, `scripts/test_airplay_mirror_runtime.c`, and `makefile:603-628`, 2026-07-22.
- The AirPlay runtime now supports audio-first and mirror-first construction while retaining one stream bridge, and partial audio-only teardown releases audio/timing resources without dispatching a player stop — verified by post-edit `read`/scoped `git diff` and Profile 13 aarch64 build, 2026-07-22.
- The current MSYS host lacks mbedTLS, libsodium, and FFmpeg development packages, so `make test-airplay` stops at dependency discovery before compiling tests; the same sources compile/link in the Switch target build — verified by `pkg-config` inventory and two Profile 13 incremental builds, 2026-07-22.
- Four queries within the 3-second rolling arming window establish a controller session; an established session refreshes on later polls/commands and consumes one timeout after 10 seconds of silence — verified by `make test-dlna-controller-session`, 2026-07-22.
- Explicit AVTransport Stop and inferred inactivity both cross into the UI only as atomic events; the main loop reuses `main_request_player_home()` and retries event consumption if the asynchronous stop request is rejected — verified by post-edit `read`, focused host tests, and Profile 13 target build, 2026-07-22.
- Existing coordinator terminal-release and shutdown ordering remain valid — verified by `test-protocol-coordinator` and `scripts/test_shutdown_order.py`, 2026-07-22.
- A clean required-feature Profile 13 build produced `NX-Cast.nro` with SHA-256 `03A692C937E4299F5EFAE10B8F7E9069522A2E6760D0DEA5E0DE844F630A20C2` — verified by clean MSYS/devkitA64 build and PowerShell `Get-FileHash`, 2026-07-22.
- Repository-wide `git diff --check` reports no whitespace errors; line-ending conversion notices reflect the existing Windows worktree configuration — verified after the clean build, 2026-07-22.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-07-22 | Step 1 | Accepted split audio-first AirPlay SETUP, retained pending audio format until mirror bridge creation, fixed audio-only cleanup, added handler/runtime regression cases, and passed Profile 13 target compilation/linking. |
| 2026-07-22 | Step 2 | Added tested atomic DLNA controller-session tracking, explicit Stop-to-home delivery, Profile 13-only inactivity exit, and main-loop return-home integration. |
| 2026-07-22 | Step 3 | Re-ran focused controller/coordinator/shutdown checks, completed a clean Profile 13 build, reviewed task symbols/diffs, and recorded the physical-test NRO hash. |
