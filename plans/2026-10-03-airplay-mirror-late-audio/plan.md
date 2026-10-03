# Plan: Screen mirroring late audio and playback controls

> Status: COMPLETED
> Created: 2026-10-03
> Last Updated: 2026-10-03

## Goal
Accept audio added to active screen mirroring without tearing down the session, and remove mirror seek controls.

## Assumptions
- Hardware audio/video continuity needs a new device test after host regression tests.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] Late audio SETUP succeeds after video packets, without disconnecting mirror transport (host regression).
- [x] A new combined bridge starts at an independently decodable keyframe; old video continues while waiting (host regression).
- [x] Mirroring has no timeline or seek input; normal URL video retains both (UI regression and backend guards).
### Non-goals
- App-specific rules, network thread changes, dependency publication, or unrelated rendering changes.
### Edge Cases
- Audio before video, teardown before replacement, absent next keyframe, and queue exhaustion.

## Design Decisions
None — protocol correctness and requested mirror control removal; reuse existing generation replacement.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Safely accept late audio and replace only at a keyframe |
| Step 2 | `steps/step-2.md` | COMPLETED | Hide and disable mirror timeline, build device binary |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Protocol tests | `make test-airplay-session` | makefile | yes |
| UI tests | `make test-ui` | makefile | yes |
| Device build | `make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1 PORTLIBS="<revision-4-prefix> /opt/devkitpro/portlibs/switch"` | verified prior build | yes |

## Context & Learnings
### Key Decisions
- Keep protocol session alive; container stream layout changes require a decoder-safe media generation change.
### Gotchas & Warnings
- Global FFmpeg is still revision 3. Device builds must explicitly use staged revision 4.
- Existing dirty files include earlier fixes and must be preserved.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| logs/run_nxlink-20261003-232036.log | Latest failures | Three late ct=8 audio SETUP failures followed by 461 and stop |
| source/protocol/airplay/media/mirror_runtime.c | Media lifecycle | Existing REPLACE queue/generation path |
| source/protocol/airplay/media/stream_bridge.c | Immutable mux header | configure_audio rejects header_written |
| source/player/ui/bar.c | UI model | Copies duration and seekable |
### Verified Facts
- Video decryption/configuration succeeds before all three audio failures in latest log.
- UxPlay lib/raop_handlers.h starts audio transport on late type-96 SETUP independently of video mux headers.
- Mirror video currently prepends SPS/PPS only while waiting for first keyframe.
- Implementation now includes SPS/PPS on all IDRs, allowing independently decodable generation changes.
- Full Trace device build uses revision 4 archives per link map; Build ID 0efec7439dff1bcf13e4e5f1c2c3437af09377de.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-03 | 1 | Full existing AirPlay host suite passes, including late audio replacement and pending stop. |
| 2026-10-03 | 2 | UI tests and Full Trace Switch build pass; device playback/audio continuity awaits user test. No remote publication performed. |
