# Plan: AirPlay Freeze Diagnostic Matrix

> Status: COMPLETED
> Created: 2026-07-22
> Last Updated: 2026-07-22

## Goal
Provide reproducible, single-variable Switch code profiles and a test playbook that identify whether the AirPlay-enabled freeze originates in concurrent protocol startup, mDNS socket setup, mDNS worker execution, thread scheduling, or the nxlink logging path.

## Assumptions
- The test computer can provide devkitPro, the existing Switch FFmpeg/mpv packages, `nxlink`, and a network connection to the same Switch.
- The target test computer is Windows with VS Code, devkitPro, and nxlink; the user owns toolchain setup and command-shell integration.
- Diagnostic behavior is compile-time only and the normal build keeps its current runtime behavior.
- This task gathers evidence and does not claim to fix the freeze.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] A single `NXCAST_DIAG_PROFILE` value selects each diagnostic variant and appears in runtime logs.
- [x] Profiles isolate AirPlay off, control-only, mDNS socket-only, mDNS idle-worker, mDNS receive-only, full parallel startup, full serial startup, and lower-priority mDNS.
- [x] Every diagnostic runtime contains a profile marker and each round follows the same timed interaction script.
- [x] Host tests validate profile mapping and serial coordinator behavior; the strict Switch trace build succeeds.
- [x] A tester can follow one document to select a profile, perform the physical test, and report results after their toolchain is configured.

### Non-goals
- Fixing AirPlay compatibility, pairing, media decoding, or the freeze during this task.
- Changing release defaults or adding runtime settings to the public UI.
- Running all variants automatically against a physical Switch without a user observing the device.

### Edge Cases
- A profile must fail the build for unknown values rather than silently use the normal configuration.
- Switching profiles must force a clean rebuild so stale object files cannot contaminate results.
- `Connection reset by peer` is recorded only as session shutdown, never as the freeze timestamp.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Diagnostic configuration | Multiple ad-hoc flags vs one named profile | One named `NXCAST_DIAG_PROFILE` mapped by Makefile to explicit compile-time controls and a numeric log ID | yes - user requested a designed multi-round test system |
| Startup comparison | Replace parallel startup vs preserve both variants | Keep current parallel default and add a diagnostic serial variant | yes - diagnosis before repair |
| Evidence transport | nxlink only vs SD-only trace vs profile markers plus existing nxlink | Runtime profile/stage markers in the existing logger; no periodic SD writes that alter timing | yes - portable and minimally invasive |
| Physical test execution | One large automated sequence vs one NRO per controlled round | One clean build and one fixed interaction script per profile | yes - isolates one variable per round |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add compile-time diagnostic controls, serial startup support, and focused host coverage. |
| Step 2 | `steps/step-2.md` | COMPLETED | Add independent runtime liveness markers that distinguish main, logger, and mDNS progress. |
| Step 3 | `steps/step-3.md` | COMPLETED | Write the physical test playbook and validate the complete matrix. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Coordinator tests | `make test-protocol-coordinator` | Existing Makefile target | yes |
| AirPlay host suite | `make test-airplay` | Existing Makefile target | yes |
| Switch build | `make clean && make NXCAST_DIAG_PROFILE=full-serial NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 -j4` | Existing strict trace contract | yes |
| Formatting | `git diff --check` | Git | yes |

## Context & Learnings
### Key Decisions
- Profile zero remains the release behavior; diagnostic profiles are opt-in and force explicit startup markers.
- The matrix distinguishes traffic volume from socket initialization and scheduling by stopping mDNS at controlled boundaries.

### Gotchas & Warnings
- nxlink uses a socket too, so missing nxlink output alone cannot prove that the main loop stopped.
- Historical logs prove mDNS has completed on this hardware before; the matrix tests interactions and scheduling rather than assuming port 5353 is intrinsically broken.
- The working tree contains extensive existing changes. This task must not revert or reformat unrelated work.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `makefile` | Existing trace flags and build contracts | `rg` found `TRACE_MEDIA`, `TRACE_INPUT`, `TRACE_AIRPLAY`, and `NXCAST_AIRPLAY_RUNTIME`. |
| `source/app/protocol_coordinator.c` | Launches all enabled service workers in parallel | `read` verified the loop in `protocol_coordinator_start()` and existing worker tests. |
| `source/protocol/airplay/discovery/mdns.c` | mDNS socket, worker, announcement, and select boundaries | `read` verified socket completion precedes worker creation and the worker announces before `select()`. |
| `source/protocol/airplay/integration.c` | Supplies receiver discovery configuration | `git diff`/`read` verified `receiver_config.enable_discovery = true`. |
| `source/main.c` | Main-loop heartbeat and profile banner | `read`/`rg` verified an existing two-second trace heartbeat and startup log boundary. |
| `source/log/log.c` | Logger worker health evidence | `read` verified worker counters and heartbeat already exist in runtime stats. |
| `logs/run_nxlink-20260722-003159.log` | Latest freeze evidence | `read` verified IPTV/DLNA reach running and output stops in the mDNS startup window. |
| `logs/run_nxlink-20260722-000659.log` | AirPlay-off control | `rg` verified continuing heartbeats and successful IPTV playback. |

### Verified Facts
- The latest freeze occurs before any AirPlay client connection or media SetURL, so high AirPlay media bandwidth is not the trigger - verified by `logs/run_nxlink-20260722-003159.log`, 2026-07-22.
- The current coordinator launches IPTV, DLNA, and AirPlay start operations concurrently, while the previous main path completed IPTV and DLNA setup before starting AirPlay - verified by current `source/app/protocol_coordinator.c` and `git show HEAD:source/main.c`, 2026-07-22.
- mDNS socket setup, initial announcement, receive/select loop, and worker priority are separable boundaries in `source/protocol/airplay/discovery/mdns.c` - verified by source read, 2026-07-22.
- Existing runtime heartbeats and logger statistics can be reused instead of adding a second logging system - verified by `source/main.c`, `source/log/log.c`, and `source/log/log.h`, 2026-07-22.
- Windows/devkitPro shell setup and nxlink invocation are explicitly user-owned and outside this task - confirmed by the user, 2026-07-22.
- `full-serial` keeps protocol startup off the main thread while enforcing IPTV, DLNA, then AirPlay worker ordering - verified by the new coordinator test and strict Switch build, Step 1.
- Socket-only, idle-thread, and receive-only mDNS modes each compile, reach `READY`, and stop cleanly in the host smoke harness; the default full mDNS suite remains passing - verified by host compile/smoke runs, Step 1.
- mDNS diagnostics live in a separate atomic state that is never cleared with the protocol state, so main-loop observation remains valid during concurrent start/stop boundaries - verified by source review and repeated host/Switch builds, Step 2.
- All named profiles pass Makefile dry-run validation, an unknown profile fails explicitly, and the strict `full-serial` Switch build embeds both the profile marker and extended runtime heartbeat - verified in Step 3.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-07-22 | Step 1 | Added eight named diagnostic profiles, supervised serial startup, four mDNS execution boundaries, configurable mDNS priority, profile banners, and focused coordinator/mDNS validation. |
| 2026-07-22 | Step 2 | Extended the existing two-second runtime heartbeat with lock-free mDNS lifecycle, worker-age, select, receive, and send evidence; added start/stop smoke assertions for all isolated mDNS modes. |
| 2026-07-22 | Step 3 | Added the ordered physical test matrix, fixed Home/IPTV/DLNA actions, heartbeat interpretation, decision tree, and result handoff format; completed host, profile, formatting, and strict Switch validation. |
