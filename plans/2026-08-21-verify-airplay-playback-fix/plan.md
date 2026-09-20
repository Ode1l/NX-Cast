# Plan: Verify AirPlay Playback Protocol Fix

> Status: COMPLETED
> Created: 2026-08-21
> Last Updated: 2026-08-21

## Goal
Verify that the current AirPlay implementation fixes the reverse-HLS empty-player and audio-only-as-mirror failures reproduced in the 2026-08-16 trace, and repair only any failing boundary.

## Assumptions
- The 2026-08-17 uncommitted AirPlay changes are intentional work that must be preserved.
- The 2026-08-16 trace predates the current pending-HLS state and audio/mirror RECORD split.
- Host AirPlay tests are the available deterministic validation before another real-device run.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [ ] Reverse-HLS rate, pause, and scrub requests remain deferred until a valid `/action` produces a playback URL.
- [ ] FCUP `/action` failures expose a specific bounded reason rather than only a generic HTTP 400.
- [ ] An audio-only SETUP does not invoke the mirror player-load path.
- [ ] Focused AirPlay tests and diff hygiene pass.

### Non-goals
- Adding new AirPlay features or changing FFmpeg, Matroska, nvtegra, or deko3d behavior.
- Rewriting the already completed 2026-08-17 ownership and detached-playback work.

### Edge Cases
- A control command arrives while reverse HLS is pending.
- `/action` carries advisory non-2xx status or a malformed/mismatched FCUP payload.
- RECORD arrives before video SETUP and only audio has been negotiated.

## Design Decisions
None — no design-sensitive changes.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Validate the current protocol fixes and repair only demonstrated regressions. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Diff hygiene | `git diff --check` | Git worktree convention | yes |
| Focused host suite | `make test-airplay` | `makefile` and completed AirPlay plans | yes |
| Switch trace build | `make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1` | `makefile` and completed AirPlay plans | if local toolchain is available |

## Context & Learnings
### Key Decisions
- Preserve the current pending-HLS state machine and separate audio/mirror callbacks; validate before editing.
### Gotchas & Warnings
- The worktree contains 21 modified AirPlay-related files from prior completed plans; do not revert or overwrite them.
- `Connection reset by peer` at application shutdown is not a playback root cause.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260816-013716.log` | Reproduction trace | Read `/action`, null URL, audio-only SETUP, and mirror claim sequence. |
| `source/protocol/airplay/media/remote_video.c` | Pending/active remote-video state | Read current state enum, deferred control, and HLS action transition. |
| `source/protocol/airplay/media/remote_hls.c` | FCUP parser and diagnostics | Read current result-specific parser. |
| `source/protocol/airplay/protocol/handlers.c` | Audio/mirror RECORD routing | Read current separate callback branches. |
| `source/protocol/airplay/media/mirror_runtime.c` | Player handoff boundary | Read current audio-only and mirror record paths. |
| `scripts/test_airplay_remote_video.c` | Reverse-HLS regression coverage | `rg` and diff show pending rate/pause/scrub tests. |
| `scripts/test_airplay_handlers.c` | RECORD routing coverage | Diff shows audio callback failure and split tests. |

### Verified Facts
- The 2026-08-16 trace returned HTTP 400 for `/action`, then issued player controls with `url=(null)` — verified by reading the trace on 2026-08-21.
- The same trace negotiated `mirror=0 audio=1` but claimed `airplay-mirror` and loaded `airplay://mirror` — verified by reading the trace on 2026-08-21.
- Current `remote_video.c` has explicit `IDLE`, `PENDING_HLS`, and `ACTIVE` states and defers controls in `PENDING_HLS` — verified by `read` on 2026-08-21.
- Current `handlers.c` routes mirror and audio RECORD through separate callbacks — verified by `read` on 2026-08-21.
- Current `remote_hls.c` reports specific FCUP action result categories — verified by `read` on 2026-08-21.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-21 | 1 | Confirmed the post-trace pending-HLS and audio/mirror split fixes; host AirPlay tests, diff hygiene, and full-trace Switch build passed without further protocol edits. |
