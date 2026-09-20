# Plan: AirPlay Missing Play Analysis

> Status: COMPLETED
> Created: 2026-09-01
> Last Updated: 2026-09-01

## Goal
Reconstruct the latest real-device AirPlay timeline and identify why later Bilibili attempts did not produce HTTP `/play` playback.

## Assumptions
- `logs/run_nxlink-20260901-223314.log` is the test run described by the user.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] Classify every later AirPlay attempt by protocol path.
- [x] Associate the visible home-screen error with its exact media owner and backend failure.
- [x] Report verified causes separately from unknown sender behavior.

### Non-goals
- Source changes before the trace establishes a receiver defect.

### Edge Cases
- HTTP remote playback and RTSP mirroring do not both use `/play`.
- A later IPTV or DLNA failure can remain visible on the home screen after AirPlay ends.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Evidence model | Search only `/play` vs reconstruct all control and ownership events | Reconstruct all paths | yes, required by observed mixed-protocol run |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Build the ordered trace and issue report. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Timeline extraction | `rg -n` and bounded `sed` over latest log | Repository logs | yes |

## Context & Learnings
### Key Decisions
- Treat HTTP `/play`, RTSP mirror setup, DLNA, and IPTV as distinct media paths.
### Gotchas & Warnings
- Do not attribute `Connection reset by peer` at shutdown to playback failure.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260901-223314.log` | Latest trace | User identified this run as YouTube, repeated Bilibili AirPlay, then DLNA. |

### Verified Facts
- Later AirPlay attempts include RTSP sessions even when no later HTTP `/play` is present.
- The run contains three HTTP `/play` requests; the third loads a Bilibili MP4 successfully.
- Sessions 18 and 19 negotiate audio-only RAOP, receive successful receiver responses, never send a video stream or `/play`, and are then torn down by the sender.
- The visible home error belongs to IPTV owner generation 5 and `cdn6.163189.xyz/163189/8tv`, which mpv rejects as an unrecognized file format.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-01 | Step 1 | Reconstructed mixed AirPlay/IPTV/DLNA timeline and separated sender-side audio-only cancellation from the unrelated IPTV error. |
