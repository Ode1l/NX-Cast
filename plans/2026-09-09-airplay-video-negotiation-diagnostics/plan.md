# Plan: AirPlay Video Negotiation Diagnostics

> Status: COMPLETED
> Created: 2026-09-09
> Last Updated: 2026-09-09

## Goal
Make one Full Trace run identify the first missing or failed boundary between AirPlay control negotiation and video playback without changing protocol behavior.

## Assumptions
- The Bilibili App-internal video path is not yet proven fixed.
- Existing per-stage logs are correct but insufficiently correlated at connection close.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [ ] Each AirPlay control connection emits a secret-free close summary correlated by connection and logical-session IDs.
- [ ] The summary distinguishes missing initial SETUP, stream SETUP, RECORD, remote `/play`, and media handoff evidence.
- [ ] Focused host tests and the Switch Full Trace build pass without changing request handling outcomes.

### Non-goals
- Fixing a sender-specific Bilibili behavior without trace evidence.
- Logging request bodies, media URLs, credentials, keys, or PIN material.

### Edge Cases
- A logical AirPlay session may use multiple TCP connections and may legitimately negotiate audio without video.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Diagnostic design | Sender-specific logging; raw packet dumps; protocol-generic stage summary | Protocol-generic, secret-free stage events and close summary | yes |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add and test connection-level negotiation stage accounting. |
| Step 2 | `steps/step-2.md` | COMPLETED | Validate Full Trace and document the exact device test procedure. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Focused tests | `make test-airplay` | repository makefile | yes |
| Switch build | `make full-trace-build` | repository makefile | yes |
| Whitespace | `git diff --check` | Git | yes |

## Context & Learnings
### Key Decisions
- Diagnostics observe existing state; they do not introduce another protocol state machine.
### Gotchas & Warnings
- Connection IDs and logical-session IDs are different and both are required for correlation.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/protocol/airplay/protocol/handlers.c` | Owns per-control-connection negotiation context | Read and route/callback search on 2026-09-09 |
| `source/protocol/airplay/media/remote_video.c` | Handles `/play` and `/stop` URL playback | Read route and lifecycle handlers on 2026-09-09 |
| `source/protocol/airplay/media/mirror_runtime.c` | Existing media bridge/player handoff traces | Trace search on 2026-09-09 |
| `scripts/test_airplay_handlers.c` | Focused host tests | Makefile source search on 2026-09-09 |

### Verified Facts
- The handler context already tracks initial SETUP, audio/video stream SETUP, RECORD request, and recording callbacks.
- `/play` is routed before RTSP method handling through `airplay_remote_video_route()`.
- Existing media traces report first config, first keyframe, bridge write/read, and player handoff, but there is no close summary stating which negotiation boundary was never reached.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-09 | Step 1 | Added protocol-neutral AirPlay negotiation summaries; `make test-airplay` passed. |
| 2026-09-09 | Step 2 | Documented the device trace procedure; Full Trace Switch build and final checks passed. |
