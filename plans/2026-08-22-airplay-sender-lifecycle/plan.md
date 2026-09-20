# Plan: AirPlay Sender Lifecycle And Render Epoch

> Status: COMPLETED
> Created: 2026-08-22
> Last Updated: 2026-08-22

## Goal
Unify each AirPlay sender's RAOP, remote-video, and reverse connections under generation-safe media lifecycle rules and prevent stale frames during replacement.

## Assumptions
- DLNA and IPTV are healthy baselines and must retain their current ownership and playback behavior.
- A LAN peer address identifies one sender device for the lifetime of concurrent AirPlay connections; Apple Session ID remains the stronger HTTP/reverse key.
- Ordinary peer close is not sufficient evidence of Stop, while protocol Stop or media-active TEARDOWN is explicit intent.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] Related RAOP, HTTP `/play`, and reverse connections from one sender share lifecycle intent without application-specific behavior.
- [x] HTTP/reverse peer close enters bounded detached state; explicit Stop or associated media-active TEARDOWN stops the matching generation.
- [x] A new `/play` atomically invalidates the previous AirPlay generation and stale events cannot affect replacement media.
- [x] First-frame diagnostics and presentation do not treat a previous media surface as the new generation's first frame.
- [x] AirPlay host tests, coordinator tests, player tests, and the Switch full-trace build pass.

### Non-goals
- Sender application, website, CDN host, advertisement, or content-title branches.
- Full screen-mirroring capability negotiation changes.
- Replacing libmpv, FFmpeg, or the existing protocol coordinator.

### Edge Cases
- RAOP may TEARDOWN before `/play` as a transport handoff and must not stop future media.
- HTTP and reverse may close before RAOP, or vice versa.
- A replacement may arrive while the previous session is attached, detached, loading, or paused.
- A stale render callback may occur after `loadfile replace` but before the new file is loaded.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Sender aggregation | Apple Session ID only; peer address only; peer address plus Apple Session ID | Use Apple Session ID for HTTP/reverse and peer address to associate concurrent RAOP transport | yes, approved by user after log analysis |
| Detach behavior | Immediate Stop; indefinite playback; bounded grace | Keep media for a bounded grace unless explicit Stop/TEARDOWN arrives | yes, approved direction |
| Replacement | Reuse mutable owner; stop then unguarded load; generation invalidation plus replacement | Invalidate old generation before effects and claim/load the replacement generation once | yes, matches existing state-machine design |
| Render boundary | Present old surface; recreate deko3d backend; gate by current media readiness | Keep backend and suppress stale first-frame/presentation until current media readiness | yes, minimal safe change |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Aggregate sender sub-connections and propagate explicit teardown intent. |
| Step 2 | `steps/step-2.md` | COMPLETED | Bound detached lifetime and enforce replacement generations. |
| Step 3 | `steps/step-3.md` | COMPLETED | Gate rendering by current-media readiness and run full validation. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Logical sessions | `make test-airplay-session` | Existing Makefile target | yes |
| Remote video | `make test-airplay-remote-video` | Existing Makefile target | yes |
| Protocol coordinator | `make test-protocol-coordinator` | Existing Makefile target | yes |
| AirPlay regression | `make test-airplay` | Existing Makefile target | yes |
| Switch build | `make full-trace-build BUILD_JOBS=4` | Existing Makefile target | yes |
| Whitespace | `git diff --check` | Git built-in | yes |

## Context & Learnings
### Key Decisions
- Aggregate transport intent before projecting it into the protocol-neutral media reducer.
- Keep blocking player effects outside session and coordinator locks.
- Treat media generation, not socket lifetime, as the render and ownership boundary.

### Gotchas & Warnings
- The worktree contains prior uncommitted AirPlay changes and must not be reset.
- A pre-`/play` RAOP TEARDOWN is a handoff, not evidence that later URL media should stop.
- Signed media URLs must not be added to fixtures or documentation.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260822-191736.log` | Runtime sequence | Shows RAOP and HTTP/reverse as separate logical sessions, detached playback, replacement, and stale rendered frames. |
| `source/protocol/airplay/protocol/logical_session.c` | Connection aggregation | Source inspection shows only Apple Session ID connections are currently bound. |
| `source/protocol/airplay/media/remote_video.c` | Remote media lifecycle | Source inspection shows indefinite detached-active and generation-based replacement. |
| `source/app/protocol_media_session.c` | Shared reducer | Source inspection verifies lease/generation stale rejection already exists. |
| `source/player/render/frontend.c` | Render boundary | Source inspection shows first-frame is keyed only by trace sequence after any successful render call. |
| `source/player/backend/libmpv.c` | Current-media readiness | Source inspection shows loadfile replacement tracks `g_file_loaded` and startup gate under the backend mutex. |

### Verified Facts
- HTTP `/play` and reverse connections bind by Apple Session ID, while RAOP remains an independent connection ID — verified by `logical_session.c` and the 2026-08-22 trace.
- The remote video layer changes active peer close to detached-active with no expiry — verified by `airplay_remote_video_session_closed`, 2026-08-22.
- The protocol reducer rejects events whose lease does not match the active generation — verified by `protocol_media_session_transition`, 2026-08-22.
- A replacement trace emitted first-frame two milliseconds after dispatch and before file-loaded, proving the trace represented an old render surface — verified by the 2026-08-22 trace.
- Existing host targets cover logical sessions, remote video, coordinator behavior, and the aggregate AirPlay suite — verified by Makefile search, 2026-08-22.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-22 | Step 1 | Associated same-peer RAOP with Apple-ID-bound HTTP/reverse sessions, propagated explicit teardown intent, and passed the aggregate AirPlay host suite. |
| 2026-08-22 | Step 2 | Added a five-second detached control lease, reattachment cancellation, explicit teardown cleanup, main-loop polling, and deterministic lifecycle tests. |
| 2026-08-22 | Step 3 | Added backend current-media readiness, gated loading/presentation and first-frame reporting, documented invariants, and passed host plus Switch validation. |
