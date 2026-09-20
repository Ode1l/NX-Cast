# Plan: Fix Real-Device AirPlay Playback Regressions

> Status: COMPLETED
> Created: 2026-08-17
> Last Updated: 2026-08-17

## Goal
Fix the two newly reproduced AirPlay playback failures: YouTube reverse HLS `/action` rejects an advisory non-2xx status, and direct URL playback is stopped when the iPhone closes its last logical control connection before the first frame.

## Assumptions
- Current UxPlay source remains the behavior reference for FCUP parsing; it treats `FCUP_Response_StatusCode` as diagnostic only.
- The coordinator/player lease remains the source of truth for shutdown and replacement.
- A pending Reverse HLS session can be discarded when its logical connections close.
- An active direct playback session should survive logical control-connection teardown until explicit `/stop`, a new claim, or application shutdown.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [ ] `airplay_remote_hls_handle_action()` accepts valid FCUP payloads regardless of the optional status value, matching UxPlay.
- [ ] Last logical connection close clears pending Reverse HLS state but does not stop or release active direct playback.
- [ ] Subsequent direct playback or shutdown can still replace/stop the retained owner without a stale-control regression.
- [ ] Host AirPlay tests, strict trace build, and release build pass.

### Non-goals
- Relaxing URL/request-id/session/playlist validation.
- Changing Control Center audio-only/mirror ownership behavior.
- Adding a timer-based connection grace period.

### Edge Cases
- `FCUP_Response_StatusCode` is missing, string-typed, zero, or non-2xx.
- Final logical connection closes while direct playback is loading, seeking, or playing.
- Final logical connection closes while Reverse HLS is pending.
- A new `/play` arrives after the previous logical connections close.
- Application shutdown while a detached direct playback owner is still active.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| FCUP status handling | Reject non-2xx vs accept any status | Accept any status as advisory and preserve strict request/URL/playlist validation | yes |
| Last logical close semantics | Stop/release active playback vs detach control state only | Clear remote-video state without stopping/releasing the active player lease | yes |
| Pending Reverse HLS close | Keep negotiation vs clear pending state | Clear pending state and reset HLS | yes |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Make FCUP response status advisory. |
| Step 2 | `steps/step-2.md` | COMPLETED | Detach active remote playback from logical connection close. |
| Step 3 | `steps/step-3.md` | COMPLETED | Update contracts and run full host/Switch validation. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Diff hygiene | `git diff --check` | Git worktree convention | yes |
| Focused host suite | `make test-airplay` | `makefile` | yes |
| Strict trace build | `make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1` | `makefile` | yes |
| Release build | `make release-build RELEASE_JOBS=4` | `makefile` | yes |

## Context & Learnings
### Key Decisions
- Keep FCUP status parsing but make the value advisory.
- Preserve active playback owner after control TCP teardown; clear remote state so new claims are accepted.
### Gotchas & Warnings
- Do not keep pending HLS state alive after its control connections disappear.
- Do not release the coordinator owner from connection close, or shutdown cleanup will lose track of the active player.
- The final logical connection close still invalidates the remote-video session id and generation so stale control requests cannot reach the retained owner.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/protocol/airplay/media/remote_hls.c` | FCUP action validation | Read `airplay_remote_hls_handle_action` |
| `scripts/test_airplay_remote_hls.c` | HLS field-validation regression | Read existing `BAD_STATUS` test |
| `source/protocol/airplay/media/remote_video.c` | Logical-close cleanup and direct playback state | Read `handle_play`, `session_closed`, `destroy` |
| `scripts/test_airplay_remote_video.c` | Session-close ownership regression | Read direct-playback transcript |
| `source/protocol/airplay/receiver.c` | Last logical connection close callback | Read `receiver_session_closed` |
| `docs/AIRPLAY_DEVELOPMENT.md` | Development/support contract | Read support and architecture sections |
| `docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md` | Ownership and route contract | Read ownership rules |
### Verified Facts
- Current UxPlay parses `FCUP_Response_StatusCode` only for a debug log and proceeds with URL/data processing — verified against a fresh read-only clone on 2026-08-17.
- Latest nxlink log shows YouTube `/action` rejected with `reason=bad-status` before any player claim — verified in `logs/run_nxlink-20260817-012917.log`.
- Latest nxlink log shows Bilibili `mpv-playback-restart` at 1106 ms, final logical connection close at about 1184 ms, then `Stop` before first frame at 1242 ms — verified in `logs/run_nxlink-20260817-012917.log`.
- `receiver_session_closed()` calls `airplay_remote_video_session_closed()` on the last bound connection — verified by reading `source/protocol/airplay/receiver.c`.
- `airplay_remote_video_session_closed()` currently stops and releases active playback — verified by reading `source/protocol/airplay/media/remote_video.c`.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-17 | 1 | Treat FCUP response status as advisory and pin the non-2xx regression. |
| 2026-08-17 | 2 | Keep active playback alive across final logical connection close; clear pending Reverse HLS state. |
| 2026-08-17 | 3 | Update protocol contracts and pass host, full-trace, and release validation. |
