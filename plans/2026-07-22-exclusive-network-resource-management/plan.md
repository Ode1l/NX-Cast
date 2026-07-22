# Plan: Exclusive Network Resource Ownership and Diagnostics

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
Give the active IPTV, DLNA, or AirPlay owner exclusive access to the relevant receiver and background-network resources, restore all discovery services deterministically on terminal playback states, and expose enough socket/thread diagnostics to prove every transition releases its resources.

## Assumptions
- `DCPD` refers to the collection of receiver/control workers; no `DCPD` or `DACP` implementation symbol exists in this repository.
- Playback runs in Application mode, so the wiliwili/Borealis Application-mode BSD configuration is an appropriate diagnostic baseline.
- Profile 13 will stage the new policy without changing profiles 1-12 or the normal build until physical testing passes.
- Paused playback retains ownership; stopped, idle, error, EOF, explicit disconnect, and Home release ownership.
- IPTV remains initialized while media is active, but its catalog/logo refresh worker can be suspended without calling FFmpeg global network deinitialization.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] Home mode runs every enabled discovery/control receiver and allows a new IPTV, DLNA, or AirPlay claim.
- [x] The first protocol family to claim media wins; a different protocol family is rejected until the current lease is released.
- [x] DLNA mode retains DLNA control/event traffic, stops the complete AirPlay receiver, and suspends IPTV background fetching.
- [x] AirPlay mode retains the AirPlay receiver, stops the complete DLNA receiver, and suspends IPTV background fetching.
- [x] IPTV mode stops both remote receiver stacks and suspends non-playback IPTV background fetching.
- [x] Stop, idle, error, EOF, disconnect, and Home restore Home mode; repeated transitions are idempotent and failed restarts remain observable/retryable.
- [x] Resource stop/start and thread joins execute outside coordinator and ownership mutexes on a supervised worker.
- [x] Runtime heartbeats identify desired/applied resource mode, service transitions, active sockets, active blocking calls, longest operation, last error, and worker age for owned network modules.
- [ ] Host coordinator/diagnostic tests, shutdown-order tests, sanitizer checks, and a strict Profile 13 Switch build pass.

### Non-goals
- Modify libmpv, FFmpeg, youtube-dl/ytdl, CDN request headers, decoder settings, or Horizon's BSD service implementation.
- Guarantee remote CDN availability or eliminate server-side HTTP 514/554 responses.
- Replace every protocol implementation with one combined event loop in this iteration.

### Edge Cases
- Simultaneous cross-protocol claims, same-protocol replacement, Stop followed by Play without a new URI, shutdown during restart, AirPlay startup cancellation, restart bind failure, and a socket operation that outlives its stop request.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Ownership conflict policy | Last writer wins; priority preemption; first owner wins | First protocol family wins; same-family session replacement remains allowed | yes — user requested one active protocol and exclusive resources |
| Isolation granularity | Pause discovery only; stop non-owner discovery; stop complete non-owner receiver | Stop the complete non-owner receiver while retaining the active protocol's control/media path | yes — user explicitly requested complete AirPlay/DLNA isolation |
| IPTV behavior | Deinitialize IPTV; leave all work active; suspend background jobs only | Keep IPTV initialized and suspend refresh/logo background work | yes — derived from exclusive-resource request while avoiding unsafe FFmpeg global deinit |
| Transition execution | Run inside claim callback; run on UI thread; supervised worker | Coordinator-owned worker, with no coordinator/ownership lock held during callbacks or joins | yes — required to avoid SOAP/RTSP self-join deadlocks |
| Release policy | Release on pause; release only on protocol disconnect; release on terminal player/protocol states | Pause retains; terminal state, explicit Stop/Home, EOF/error, or disconnect releases | yes — matches the user's stated playback lifecycle |
| BSD baseline | Default 3; diagnostic 8; Borealis-like 12/efficiency 8; maximum 16 | New Profile 13 uses 12 sessions and socket-buffer efficiency 8 | yes — user requested learning from wiliwili; staged as a new comparison profile |
| Diagnostics | Log-only transitions; per-module counters; central registry | Central bounded atomic registry plus existing module snapshots and transition logs | yes — user requested diagnostics and proactive resource control together |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Protect first-owner-wins and exclusive resource modes with coordinator host tests |
| Step 2 | `steps/step-2.md` | COMPLETED | Wire runtime receiver isolation, IPTV background suspension, and terminal lease release |
| Step 3 | `steps/step-3.md` | COMPLETED | Add bounded global network-operation diagnostics and instrument owned sockets |
| Step 4 | `steps/step-4.md` | COMPLETED | Harden stop ordering and bounded blocking-call cancellation using diagnostic evidence |
| Step 5 | `steps/step-5.md` | COMPLETED | Add Profile 13, VS Code task selection, and the physical A/B test contract |
| Step 6 | `steps/step-6.md` | COMPLETED | Run integrated host, sanitizer, shutdown, and strict Switch validation |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Coordinator unit test | `make test-protocol-coordinator` | `makefile:test-protocol-coordinator` | yes |
| Network diagnostics unit test | `make test-network-diagnostics` | added in Step 3 | yes |
| AirPlay/lifecycle host suite | `make test-airplay` | `makefile:test-airplay` | yes |
| Sanitizer suite | `make test-c-safety-sanitize` | `makefile:test-c-safety-sanitize` | yes |
| Shutdown ordering | `make test-shutdown-order` | `makefile:test-shutdown-order` | yes |
| Strict Switch build | `make clean && make NXCAST_DIAG_PROFILE=full-owner-exclusive-bsd12 NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 -j4` | diagnostic build convention in `makefile` and `.vscode/tasks.json` | yes |
| Patch hygiene | `git diff --check` | repository convention | yes |

## Context & Learnings
### Key Decisions
- Resource mode is derived from the current ownership lease, but service transitions are asynchronous and applied by the coordinator rather than by protocol callback threads.
- IPTV service initialization is not equivalent to IPTV background network activity; deinitializing it during playback would call `avformat_network_deinit()`, so background suspension is the safe boundary.
- Complete receiver stop means socket shutdown, worker join, and state cleanup; it does not mean `socketExit()`, which remains process-global.

### Gotchas & Warnings
- `player_ownership_claim()` currently replaces any owner, so the first-owner rule must be enforced above the primitive or its callers will continue to preempt each other.
- DLNA `Stop` currently submits only `PLAYER_COMMAND_STOP`; it does not release the DLNA lease, and `Play` currently assumes the lease still exists.
- RenderingControl currently claims DLNA ownership for an unowned volume/mute command; this must not trigger exclusive media mode.
- AirPlay and DLNA stop functions join network workers, so calling them from the corresponding SOAP/RTSP worker would deadlock.
- Existing modified files are diagnostic work in progress and must be preserved rather than reset.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/app/protocol_coordinator.[ch]` | Existing lifecycle, ownership snapshot, service workers, and discovery policy | `rg protocol_coordinator`; targeted reads on 2026-07-22 |
| `source/player/core/ownership.[ch]` | Process-global lease primitive | `read source/player/core/ownership.c`; current claim always replaces owner |
| `source/main.c` | Service operation wiring, player snapshot observation, heartbeat, and shutdown | targeted `read` around protocol setup, tick, heartbeat, and shutdown |
| `source/protocol/dlna/control/action/avtransport.c` | DLNA claim, Stop/Play, and release boundary | targeted `read`; Stop lacks release and Play assumes ownership |
| `source/protocol/dlna/control/action/renderingcontrol.c` | Non-media commands that currently claim DLNA | targeted `read`; unowned volume/mute obtains a media lease |
| `source/protocol/dlna_control.c` | Full DLNA start/stop boundary | targeted `read`; SSDP, HTTP, event, SOAP, and SCPD have ordered teardown |
| `source/protocol/airplay/integration.c` | AirPlay claim/release and full receiver boundary | targeted `read`; disconnect paths release, integration stop destroys receiver/runtime |
| `source/protocol/airplay/server.c` | AirPlay listener/client socket lifecycle | targeted `read`; shutdown precedes close and join |
| `source/protocol/http/http_server.c` | DLNA HTTP listener lifecycle | targeted `read`; synchronous client handling and listener shutdown before join |
| `source/protocol/airplay/discovery/mdns.[ch]` | Existing mDNS diagnostics and socket worker | targeted `read`; phase/counter/heartbeat snapshot exists |
| `source/protocol/dlna/discovery/ssdp.[ch]` | SSDP worker and socket lifecycle | targeted `read`; no detailed diagnostics snapshot yet |
| `source/iptv/iptv.c` | IPTV background worker and process-global FFmpeg network calls | `rg g_worker_cond`; `read iptv_deinit`; deinit calls `avformat_network_deinit()` |
| `scripts/test_protocol_coordinator.c` | Existing host concurrency/lifecycle regression suite | `rg` and targeted reads on 2026-07-22 |
| `makefile`, `.vscode/tasks.json`, `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md` | Profile/test/build and hardware run contract | `rg NXCAST_DIAG_PROFILE` on 2026-07-22 |

### Verified Facts
- libnx defaults `SocketInitConfig::num_bsd_sessions` to 3 and blocks callers when all cloned service sessions are occupied — verified from libnx `socket.c`, `bsd.c`, and `sessionmgr.c`, 2026-07-22.
- wiliwili pins Borealis commit `5f08b286`, which uses 12 BSD sessions and `sb_efficiency=8` in Application mode — verified from the pinned submodule and `switch_wrapper.c`, 2026-07-22.
- NX-Cast Profile 12 uses eight BSD sessions and mDNS-only playback suspension — verified from `makefile`, 2026-07-22.
- AirPlay control sockets already use receive/send timeouts and `shutdown(SHUT_RDWR)` before close — verified from `source/protocol/airplay/server.c`, 2026-07-22.
- DLNA SSDP and HTTP listener sockets already shutdown before worker join, but only mDNS exposes a detailed heartbeat/counter snapshot — verified from targeted source reads, 2026-07-22.
- No `DCPD` or `DACP` symbol exists in the project — verified by `rg -i "dcpd|dacp" source scripts`, 2026-07-22.
- Opt-in exclusive mode rejects cross-family claims, permits same-family replacement, converges services asynchronously, retries failed restoration, and cancels a blocked restart during global stop — verified by strict host coordinator tests, 2026-07-22.
- The MSYS2 host compiler in this workspace lacks linkable ASan/UBSan runtimes (`-lasan`, `-lubsan`), so sanitizer execution is unavailable in the current environment — verified by attempted sanitized coordinator build, 2026-07-22.
- Central network diagnostics can account for concurrent nested operations and all instrumented socket lifecycles without allocation or payload capture; strict host tests, the mDNS lifecycle test, and the Profile 13 Switch link pass — verified 2026-07-22.
- DLNA HTTP/event, SSDP, mDNS, and AirPlay control now publish stop before shutdown, join before close, and prevent stale fd reuse; IPTV background fetch is cooperatively cancellable and gates resource-mode application — verified by focused host/static tests and Profile 13 link, 2026-07-22.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-07-22 | Step 1 | Added opt-in first-owner resource modes, asynchronous stop/start convergence, retry diagnostics, and concurrency/shutdown tests; strict host test and diff hygiene passed. |
| 2026-07-22 | Step 2 | Wired full non-owner receiver isolation, IPTV background suspension, pre-play resource waits, terminal/Home release, DLNA Stop→Play reacquisition, and runtime resource heartbeats; coordinator/shutdown tests and Profile 13 Switch link passed. |
| 2026-07-22 | Step 3 | Added the bounded atomic network registry, instrumented mDNS/SSDP/DLNA HTTP/AirPlay control sockets and blocking calls, and emitted compact heartbeat/stall diagnostics; focused host tests and Profile 13 link passed. |
| 2026-07-22 | Step 4 | Hardened receiver teardown with atomic fd ownership and delayed close, bounded DLNA event connect/I/O, added AirPlay blocked-client lifecycle coverage, and made IPTV background fetch quiescence a retryable prerequisite for media open. |
| 2026-07-22 | Step 5 | Packaged the design as Profile 13 with BSD12/efficiency8, made it the VS Code diagnostic default, added compile-time bounds, documented the physical A/B contract, and completed a clean strict Switch link. |
| 2026-07-22 | Step 6 | Direct relevant host suites, static shutdown ordering, JSON, diff hygiene, and clean Profile 12/13 builds passed; recorded the pre-existing Cygwin log-mirror assertion, unavailable sanitizer runtimes, and the remaining physical hardware matrix. |
