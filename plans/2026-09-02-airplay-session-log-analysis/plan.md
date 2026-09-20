# Plan: AirPlay Session Log Analysis

> Status: COMPLETE
> Created: 2026-09-02
> Last Updated: 2026-09-02

## Goal
Reconstruct the three latest AirPlay trials and identify evidence-backed protocol, media, metadata, and lifecycle defects without changing code.

## Assumptions
- `logs/run_nxlink-20260902-213415.log` contains the three trials described by the user in chronological order.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] Each trial is mapped from negotiation through media handoff and disconnect/stop.
- [x] Missing title and detached control are classified at their earliest observable boundary.

### Non-goals
- Production code changes in this analysis pass.

### Edge Cases
- A sender may create separate RTSP audio and HTTP video connections under one AirPlay interaction.

## Design Decisions
None — no design-sensitive changes.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETE | Correlate the three sessions and report root causes. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Timeline extraction | `rg`/`sed` against latest log | local logs | yes |
| Source correlation | `rg` against AirPlay protocol/media source | repository | yes |

## Context & Learnings
### Key Decisions
- Treat sender connection closure as an observation, not automatically as the cause.
### Gotchas & Warnings
- Shutdown-time `Connection reset by peer` is unrelated to playback failure.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260902-213415.log` | latest three-session runtime trace | newest file by modification time |
| `source/protocol/airplay/media/remote_video.c` | `/play`, metadata and control lifecycle | route implementation |
| `source/protocol/airplay/protocol/logical_session.c` | connection-to-logical-session mapping | ownership implementation |

### Verified Facts
- The latest available log is `run_nxlink-20260902-213415.log` — verified by `ls -lt logs`, 2026-09-02.
- Both Bilibili trials used direct MP4 and reached a hardware-rendered first frame; their failures occur after media startup, not during decoding.
- RAOP sessions remain unbound connection-local IDs, while HTTP `/play` and `/reverse` use an `X-Apple-Session-ID` logical ID. A RAOP `TEARDOWN` therefore cannot terminate the active HTTP video owner.
- Closing the final HTTP/reverse connection only marks active remote video control detached and deliberately retains playback.
- `selectedMediaArray` is acknowledged but not parsed; binary `/play` metadata only copies `clientProcName`, so the actual media title is not propagated.
- The YouTube reverse-HLS trial loaded all required playlists, maintained control polling, and ended through an explicit `/stop`. Its TLS error occurred after media release during teardown.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-02 | Step 1 | Reconstructed two Bilibili direct-MP4 sessions and one YouTube reverse-HLS session; isolated lifecycle identity and metadata mapping defects. |
