# Plan: Mirror audio clock epoch correction

> Status: COMPLETED
> Created: 2026-10-04
> Last Updated: 2026-10-04

## Goal
Correct protocol-defined audio clock normalization and expose why mirrored audio is discarded before mpv.

## Assumptions
- Device audio may have further decoder/output issues; audible playback cannot be verified on host.
- Latest trace lacks raw clock values, so exact device skew remains unknown.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] A negotiated NTP session whose video timestamps omit the NTP epoch delta attaches audio without video reload.
- [x] No-NTP tests remain unchanged; no heuristic epoch guessing or relaxed skew threshold.
- [x] Dropped audio logs distinguish missing sync and skew with bounded raw timing diagnostics.
### Non-goals
- Network/thread redesign, app-specific workarounds, standalone audio, releases.
### Edge Cases
- Audio re-SETUP; NTP era wrap; missing video/sync; cancelled stream.

## Design Decisions
None - correct the existing timestamp contract at the negotiated transport boundary, retaining the independent audio path.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | steps/step-1.md | COMPLETED | Protocol normalization, focused regression, bounded diagnostics and device binary |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Regression | make test-airplay | makefile | yes |
| Whitespace | git diff --check | Git | yes |
| Build | make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1 PORTLIBS="<revision-4 staged prefix> /opt/devkitpro/portlibs/switch" | previous successful build | yes |

## Context & Learnings
### Key Decisions
- Use negotiated NTP mode, not arbitrary time offsets inferred from packets.
- Keep video code unchanged; inspect clock drops even when zero audio packets reach mux.
### Gotchas & Warnings
- Root/global FFmpeg is still revision 3; use staged revision 4.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| logs/run_nxlink-20261004-013104.log | Device failure | NTP negotiation; delivered 993/2668; no audio-add or mux progress |
| source/protocol/airplay/media/mirror_runtime.c | Sync callback transport context | Uses NTP boolean currently discarded after prepare |
| source/protocol/airplay/mirror/clock.c | Bounded audio mapping | Wrong epoch causes out-of-range drops |
| others/UxPlay-master/lib/raop_ntp.c (sibling repository) | Reference | raop_remote_timestamp_to_nano_seconds subtracts epoch in NTP mode |
### Verified Facts
- Latest trace negotiates timing=NTP, receives audio control packets and decrypted frames, no audio-add command.
- UxPlay audio uses protocol-aware NTP epoch correction; mirror video conversion does not subtract this epoch.
- Current NX-Cast runtime sends unnormalized audio NTP to the shared video timeline; synthetic tests previously use identical clock origins.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-04 | Step 1 | Reproduced missing audio activation with realistic NTP delta; corrected negotiated clock domain; host suite and staged revision-4 Full Trace build passed. Audible AAC-ELD output remains unverified on hardware. |
