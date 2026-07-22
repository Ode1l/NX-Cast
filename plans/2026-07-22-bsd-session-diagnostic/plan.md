# Plan: BSD Session Pool Diagnostic

> Status: COMPLETED
> Created: 2026-07-22
> Last Updated: 2026-07-22

## Goal
Add isolated receive-only profiles that determine whether enlarging the libnx BSD service-session pool fixes the shared-network stall and DLNA instability.

## Assumptions
- Eight BSD sessions fit the application resource budget and provide enough concurrency for the diagnostic experiment.
- Device-side runtime validation remains the user's responsibility after the NRO builds.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] The new profile retains receive-only mDNS behavior and default mDNS thread priority.
- [x] The new profile initializes libnx sockets with `num_bsd_sessions=8` and logs the selected count.
- [x] VS Code exposes the new profile and a strict build produces `NX-Cast.nro`.
- [x] A matching BSD16 profile builds successfully for the capacity-vs-design device comparison.

### Non-goals
- Declaring the larger session pool a production fix before device validation.
- Changing player, AirPlay protocol, or mDNS packet-processing behavior.

### Edge Cases
- Normal and existing diagnostic profiles must retain `socketInitializeDefault()` behavior.

## Design Decisions
None — no design-sensitive changes.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add and build the isolated BSD-session diagnostic profile. |
| Step 2 | `steps/step-2.md` | COMPLETED | Add and build the BSD16 capacity comparison profile. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Config syntax | `ConvertFrom-Json .vscode/tasks.json` | Existing VS Code configuration | yes |
| Formatting | `git diff --check` | Repository convention | yes |
| Build | strict `make ... NXCAST_DIAG_PROFILE=mdns-receive-bsd8 ... -j4` from the short MSYS path | Existing diagnostic build contract | yes |
| BSD16 build | strict `make ... NXCAST_DIAG_PROFILE=mdns-receive-bsd16 ... -j4` from the short MSYS path | Step 2 comparison contract | yes |

## Context & Learnings
### Key Decisions
- Use a distinct Profile rather than changing the production default so the experiment isolates one variable.
### Gotchas & Warnings
- The lower-priority full profile keeps idle networking alive but does not restore IPTV, DLNA, or AirPlay playback, so priority alone is not a sufficient fix.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/main.c` | Initializes the libnx socket driver | `initialize_network()` currently calls `socketInitializeDefault()`. |
| `makefile` | Maps named profiles to compile-time diagnostic behavior | Existing profile table defines mDNS modes and IDs. |
| `.vscode/tasks.json` | Exposes selectable diagnostic profiles | Existing `nxcastDiagProfile` pick list. |
| `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md` | Defines the diagnostic matrix and expected boundaries | Existing profile table and decision tree. |
### Verified Facts
- The failing `mdns-receive` run loses nxlink immediately after the mDNS worker starts — verified from `logs/run_nxlink-20260722-020734.log`, 2026-07-22.
- Under `full-low-priority`, the AirPlay session stalls both logger progress and mDNS select progress while the main and media actor heartbeats continue; both socket paths recover after teardown — verified from `logs/run_nxlink-20260722-021621.log`, 2026-07-22.
- The installed libnx `SocketInitConfig` exposes `num_bsd_sessions`, documented as typically 3 — verified from `D:/devkitPro/libnx/include/switch/runtime/devices/socket.h`, 2026-07-22.
- The clean `mdns-receive-bsd8` build produced an NRO containing the new profile and socket-session log markers — verified by build exit 0 and binary marker search, 2026-07-22.
- BSD8 restores UI, nxlink, IPTV, and DLNA connectivity, but several DLNA videos still produce repeated FFmpeg HTTP truncation/reconnects and unreliable seeking — verified from device report and `logs/run_nxlink-20260722-022510.log`, 2026-07-22.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-07-22 | Step 1 | Added the isolated eight-session receive profile and completed a strict clean Switch build. |
| 2026-07-22 | Step 2 | Added the BSD16 capacity comparison and completed a strict clean Switch build. |
