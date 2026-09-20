# Plan: AirPlay HLS and IPTV Regression

> Status: COMPLETED
> Created: 2026-09-02
> Last Updated: 2026-09-02

## Goal
Identify and repair evidence-backed AirPlay negotiation/media-bridge defects and the IPTV playback regression without changing the historically stable serialized DLNA HTTP model.

## Assumptions
- `v0.2.0` is the last useful pre-AirPlay baseline for shared player and IPTV behavior.
- `logs/run_nxlink-20260902-012957.log` is the latest complete mixed-protocol trace available locally.
- Existing AirPlay protocol and media actor boundaries should be corrected rather than replaced.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [ ] Shared libmpv/FFmpeg options no longer apply AirPlay reverse-HLS behavior to IPTV or DLNA.
- [ ] AirPlay negotiation and bridge state have a single traceable route from accepted request to submitted media command.
- [ ] Focused tests and the available Switch build validation pass.
- [ ] DLNA HTTP remains serialized unless direct evidence proves it blocks playback.

### Non-goals
- Sender-specific Bilibili or YouTube hacks.
- A new DLNA HTTP worker pool.
- Replacing libmpv, FFmpeg, or the protocol coordinator.

### Edge Cases
- Direct URL AirPlay, reverse HLS, mirror transport, audio-only setup, channel switches, and live IPTV playlists must use isolated media policies.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| DLNA HTTP concurrency | Keep serialized; add client worker pool | Keep serialized because `v0.2.0` used the same model successfully and SOAP ordering is simpler | yes — follows the user's regression-first concern |
| Compatibility fixes | Sender-specific branches; protocol/media-type policies | Protocol and media-type policies only | yes — follows the user's prior first-principles requirement |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Locate the earliest AirPlay and IPTV regressions against source and runtime evidence. |
| Step 2 | `steps/step-2.md` | COMPLETED | Isolate shared player/HLS policies and repair the IPTV regression. |
| Step 3 | `steps/step-3.md` | COMPLETED | Repair evidence-backed AirPlay negotiation/bridge state defects. |
| Step 4 | `steps/step-4.md` | COMPLETED | Run focused tests/build checks and record device validation targets. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Focused host tests | `make test-host` or discovered focused test targets | `Makefile` | yes |
| Switch compile | discovered repository build command that does not upload | `Makefile`, `.vscode/tasks.json` | yes when dependencies are available |
| Regression diff | `git diff --check` and bounded `git diff` | Git | yes |

## Context & Learnings
### Key Decisions
- Preserve independent network receiver threads and serialize only player commands through the media actor.
- Diagnose IPTV from shared player options and live-HLS behavior before touching network concurrency.
### Gotchas & Warnings
- NX-Cast socket instrumentation does not count sockets opened internally by FFmpeg.
- Shutdown-time `Connection reset by peer` is not a playback failure.
- Absence of `/play` means the failure precedes the player bridge.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/player/backend/libmpv.c` | Shared libmpv and FFmpeg policy | compare current and `v0.2.0` options |
| `source/player/cache_policy.c` | Per-transport cache isolation | focused host test and diff review |
| `scripts/test_player_cache_policy.c` | IPTV, MP4, reverse-HLS policy contract | `make test-player-cache-policy` passed |
| `source/player/core/session.c` | Media command and policy boundary | current media actor submission path |
| `source/protocol/airplay/` | Negotiation, reverse HLS, and bridge lifecycle | current route and worker implementation |
| `source/iptv/iptv.c` | IPTV open/switch behavior | compare current and `v0.2.0` flow |
| `logs/run_nxlink-20260902-012957.log` | Latest mixed-protocol runtime evidence | earliest-error correlation |

### Verified Facts
- `v0.2.0` and current DLNA HTTP both accept and handle one client synchronously on one listener thread — verified by `git show` and source read, 2026-09-02.
- Current runtime reported no instrumented socket pressure, actor timeout/rejection, or coordinator transition failure — verified by latest trace, 2026-09-02.
- Current build keeps protocol receivers active during media playback — verified by `NXCAST_KEEP_RECEIVERS_DURING_MEDIA=1` and coordinator initialization, 2026-09-02.
- `v0.2.0` left ordinary HLS on mpv defaults and only applied a 2 second / 8 MiB / 2 MiB cache to direct MP4; commit `a9a8e62` changed every network URI to 20 seconds / 20 MiB / 10 MiB — verified by Git history, 2026-09-02.
- Latest IPTV trace repeatedly skipped expired HLS segments and advanced audio PTS in 20 second steps while actor, coordinator, and instrumented sockets remained healthy — verified by `logs/run_nxlink-20260902-012957.log`, 2026-09-02.
- Latest AirPlay trace completed `/play` and FCUP acquisition; failures followed bridge handoff: VP9 was selected despite available H.264, and seek crossed CDN hosts while FFmpeg reused the previous HTTP connection — verified by the mixed trace, 2026-09-02.
- Current reverse-HLS rewrite prefers H.264 when advertised and the transport policy disables HTTP persistence only for the local AirPlay proxy; focused AirPlay tests pass — verified by source read and `make test-airplay`, 2026-09-02.
- `make -j4` built `NX-Cast.nro`, and `git diff --check` passed, 2026-09-02.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-02 | Step 1 | Located the IPTV regression in the blanket network cache policy; excluded DLNA worker concurrency as a causal change. |
| 2026-09-02 | Step 2 | Restored mpv defaults for ordinary HLS, restored the v0.2.0 direct-MP4 cache, and isolated reverse-HLS transport options. |
| 2026-09-02 | Step 3 | Verified protocol negotiation reaches the bridge and retained protocol-neutral H.264 selection, cross-host HTTP, replacement, and ownership fixes. |
| 2026-09-02 | Step 4 | Passed the AirPlay host suite, cache-policy test, Switch build, and diff checks; device-only nvtegra behavior remains for retest. |
