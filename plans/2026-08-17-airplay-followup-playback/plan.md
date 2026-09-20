# Plan: Correct YouTube HLS And Detached Playback Cleanup

> Status: COMPLETED
> Created: 2026-08-17
> Last Updated: 2026-08-17

## Goal
Fix the remaining real-device AirPlay failures: accept YouTube condensed media playlists with empty parameters, and stop retained direct playback after a bounded logical-close grace period unless a new AirPlay play cancels cleanup.

## Assumptions
- The FCUP advisory-status and cross-protocol takeover fixes remain correct and must not be reverted.
- YouTube's second FCUP response is a condensed media playlist, and its empty `PARAMS=""` form is the current rewrite failure boundary.
- The iPhone can close AirPlay HTTP control sockets before or shortly after the first frame; the receiver should keep playback briefly, then stop if no replacement play arrives.
- The Switch main loop can drive a per-frame AirPlay cleanup tick.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [ ] A synthetic YouTube condensed media playlist with `PARAMS=""` rewrites successfully into absolute segment URLs.
- [ ] A new `/play` cancels pending detached-playback cleanup and replaces the retained owner.
- [ ] Final logical connection close schedules bounded cleanup; playback is not kept indefinitely when the iPhone moves on.
- [ ] Existing FCUP validation, cross-protocol takeover, and strict build gates still pass.

### Non-goals
- Implementing full generic HLS parsing beyond the existing bounded playlist path.
- Reverting advisory FCUP status handling or cross-protocol takeover.
- Adding user-visible settings for the cleanup delay.

### Edge Cases
- Empty `PARAMS`, empty `PREFIX`, non-condensed media playlist, or URL/attribute bounds.
- New `/play` arrives before, during, or after the cleanup deadline.
- Explicit `/stop`, cross-protocol takeover, natural end, or shutdown races with pending cleanup.
- Last logical connection close occurs while direct playback is loading, seeking, or already stopped.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Detached playback lifetime | Keep indefinitely vs stop immediately vs bounded grace | Bounded 1500 ms grace, canceled by a new successful claim | yes |
| Condensed YouTube format | Require non-empty attributes vs support empty `PARAMS` | Support empty `PARAMS` and preserve the original suffix after the prefix | yes |
| Cleanup drive point | AirPlay worker timer vs main-loop tick | Main-loop `airplay_integration_tick()` for monotonic cleanup | yes |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Fix condensed HLS expansion and add the YouTube empty-parameter regression test. |
| Step 2 | `steps/step-2.md` | COMPLETED | Add bounded detached playback cleanup and new-play cancellation. |
| Step 3 | `steps/step-3.md` | COMPLETED | Update protocol lifecycle documentation. |
| Step 4 | `steps/step-4.md` | COMPLETED | Run full host, trace, and release validation. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Diff hygiene | `git diff --check` | Git worktree convention | yes |
| Host regression suite | `make test-airplay` | `makefile` | yes |
| Strict trace build | `make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1` | `makefile` | yes |
| Release build | `make release-build RELEASE_JOBS=4` | `makefile` | yes |

## Context & Learnings
### Key Decisions
- YouTube condensed playlists are expanded before media URI resolution.
- Logical close cleanup needs both a deadline and cancellation by a newer claim.
### Gotchas & Warnings
- Do not replace absolute remote URLs with the local HLS route for condensed segment tails.
- Never release an owner if a newer claim replaced the pending detached lease.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/protocol/airplay/media/remote_hls.c` | Condensed HLS expansion and rewrite failure | Read `hls_expand_condensed` |
| `scripts/test_airplay_remote_hls.c` | HLS protocol regression fixtures | Read current master/media tests |
| `source/protocol/airplay/media/remote_video.h` | Remote video ops and session-close callback contract | Read ops struct |
| `source/protocol/airplay/media/remote_video.c` | Final logical close behavior | Read `session_closed` |
| `source/protocol/airplay/integration.c` | Retained lease and cleanup lifecycle | Read claim/release/stop paths |
| `source/main.c` | Per-frame integration hook | Read main render loop |
| `scripts/test_airplay_remote_video.c` | Detached playback regression coverage | Read close/re-entry tests |

### Verified Facts
- Latest trace rejects the second YouTube `/action` at 103718 bytes with `reason=rewrite-failed` — verified in `logs/run_nxlink-20260817-020826.log` lines 293-297.
- Latest trace presents the Bilibili first frame before final logical close, then leaves `own=airplay-video/2` playing for tens of seconds after close — verified in the same log lines 730-766.
- UxPlay expands `#YT-EXT-CONDENSED-URL` media playlists and supports an empty `PARAMS` string — verified by reading its `adjust_yt_condensed_playlist()` implementation.
- Current `hls_parse_quoted_attribute()` rejects an empty quoted value because it requires `end > start` — verified by reading `source/protocol/airplay/media/remote_hls.c`.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-17 | 1 | Added empty-parameter condensed HLS expansion and YouTube-style host regression coverage. |
| 2026-08-17 | 2 | Added remote-control detach callback, 1500 ms cleanup deadline, new-play cancellation, and per-frame integration cleanup. |
| 2026-08-17 | 3 | Updated development and protocol contracts for condensed HLS and bounded detached cleanup. |
| 2026-08-17 | 4 | Passed host, trace, and release validation; rebuilt the final trace NRO. |
