# Plan: Restore AirPlay Media Negotiation

> Status: COMPLETED
> Created: 2026-08-14
> Last Updated: 2026-08-14

<!--
  Plan-level status (lifecycle):
    DRAFT     — awaiting approval after clarification
    ACTIVE    — execution in progress
    COMPLETED — all steps done, verified
    ARCHIVED  — optional long-term archival state
  This is distinct from step-level status (PENDING|IN_PROGRESS|COMPLETED|BLOCKED)
  in `steps/step-N.md`. The pre-edit gate checks step status, not plan status.
-->

## Goal
Make current iOS clients proceed from successful AirPlay pairing to either H.264 screen mirroring or URL/HLS playback without regressing DLNA/IPTV.

## Assumptions
- The latest trace represents both video-player AirPlay and Control Center mirroring attempts from the same iPhone.
- Existing PlayFair, pairing, remote-video, mirror runtime, FFmpeg, and player implementations remain in scope and should be repaired rather than replaced.
- Standalone audio-only AirPlay remains a non-goal; audio is required only as part of video playback or screen mirroring.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] `POST /audioMode` and valid empty `SET_PARAMETER` keep an established AirPlay session alive with HTTP/RTSP 200 responses.
- [x] NX-Cast advertises video and HLS support whenever its remote-video route is installed, while preserving screen-mirroring and audio feature bits.
- [x] Host AirPlay tests, mDNS smoke tests, and a strict Switch AirPlay build pass.
- [x] DLNA/IPTV code paths and ownership behavior are unchanged.

### Non-goals
- Standalone AirPlay music playback, AirPlay 2 multi-room audio, HEVC mirroring, or a new media backend.
- Changes to FFmpeg decoding, DLNA, IPTV, or the player UI.

### Edge Cases
- Unknown `/audioMode` payload values are acknowledged but do not mutate playback state.
- Non-empty unsupported `SET_PARAMETER` content types still return 400.
- Builds without a remote-video handler must continue clearing video/HLS feature bits.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| AirPlay compatibility baseline | Preserve strict 501/400 responses vs match UxPlay's tolerant control contract | Match UxPlay for `/audioMode` and empty `SET_PARAMETER` while retaining validation for non-empty bodies | yes — user requested UxPlay-derived compatibility and direct implementation |
| Remote video discovery | Keep UxPlay default HLS-off feature mask vs advertise implemented NX-Cast routes | Add video/HLS bits only when `remote_video` is present | yes — user requested both mirroring and URL/HLS video |
| Media pipeline | Modify FFmpeg/player vs repair negotiation before media reaches them | Repair negotiation only | yes — trace verifies DLNA hardware playback succeeds and no AirPlay media reaches FFmpeg |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Make the RTSP control transcript compatible with current iOS behavior. |
| Step 2 | `steps/step-2.md` | COMPLETED | Advertise the already-implemented AirPlay URL/HLS route and test discovery output. |
| Step 3 | `steps/step-3.md` | COMPLETED | Run regression and strict Switch build validation and record the real-device test contract. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Focused transcript | `make test-airplay` | `makefile:648-688` | yes |
| mDNS smoke | `python3 scripts/smoke_airplay_mdns.py` | `scripts/smoke_airplay_mdns.py` | yes |
| Switch build | `source /opt/devkitpro/switchvars.sh && make TRACE_MEDIA=1 TRACE_INPUT=1 TRACE_AIRPLAY=1 NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 NXCAST_REQUIRE_AIRPLAY_MUXER=1 -j4` | `makefile`, `.vscode/tasks.json` | yes |
| Diff hygiene | `git diff --check` | Git | yes |

## Context & Learnings
### Key Decisions
- Treat this as a control-plane negotiation failure because the trace contains no mirror TCP connection, video access unit, bridge push, or AirPlay player load.
- Keep the UxPlay-compatible base mask `0x5A7FFEE6`, then add bits 0 and 4 for NX-Cast's existing remote-video implementation.

### Gotchas & Warnings
- The worktree already contains uncommitted AirPlay, build, documentation, and cache-policy work; edits must preserve it.
- The system Switch FFmpeg package may still be older than the staged AirPlay-capable build, so strict build evidence must record the actual selected package path.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260814-003914.log` | Real-device control transcript | `rg`/`sed` show successful pairing and audio SETUP followed by `/audioMode` 501, empty `SET_PARAMETER` 400, and no video setup |
| `source/protocol/airplay/protocol/handlers.c` | RTSP route and SETUP response implementation | `read` confirms `/audioMode` is unhandled and empty `SET_PARAMETER` is rejected |
| `source/protocol/airplay/integration.c` | Receiver feature configuration | `read` confirms only `AIRPLAY_MDNS_FEATURES_MIRROR_COMPAT` is supplied despite a live remote-video handler |
| `source/protocol/airplay/receiver.c` | Conditional feature filtering | `read` confirms video/HLS bits are only cleared, never added |
| `scripts/test_airplay_handlers.c` | Control transcript regression coverage | `read` confirms existing SETUP/RECORD transcript can be extended |
| `scripts/smoke_airplay_mdns.py` | Discovery contract coverage | `rg` confirms the old `0x5A7FFEE6,0x0` value is asserted |
| `/tmp/uxplay-reference.vjph6D/lib/raop_handlers.h` | Read-only GPL compatibility reference | `read` confirms UxPlay acknowledges `/audioMode` and returns audio data/control ports |

### Verified Facts
- Pairing, FairPlay, initial SETUP, RECORD, and ALAC `ct=2` audio socket creation all succeed on Switch — verified by `logs/run_nxlink-20260814-003914.log`, 2026-08-14.
- The iPhone tears down one session about one second after `/audioMode` receives 501, and no session sends a type-110 mirror SETUP — verified by bounded `rg`/`sed` transcript analysis, 2026-08-14.
- DLNA hardware playback succeeds later in the same trace, so this failure occurs before FFmpeg/deko3d — verified by the latest log, 2026-08-14.
- UxPlay uses the same base feature mask but conditionally enables bits 0 and 4 when HLS support is enabled — verified from the official UxPlay GitHub source cloned read-only to `/tmp`, 2026-08-14.
- NX-Cast already implements `/play`, `/scrub`, `/rate`, `/stop`, and related remote-video routing, but does not advertise the associated video/HLS bits — verified by `rg`/`read` of `remote_video.c`, `receiver.c`, and `integration.c`, 2026-08-14.
- A transcript matching the real iOS order now acknowledges valid `/audioMode` and empty `SET_PARAMETER`, while rejecting unsupported non-empty parameter payloads — verified by `make test-airplay`, 2026-08-14.
- Production discovery now emits `0x5A7FFEF7,0x0`, preserving the UxPlay mirror profile while adding implemented video/HLS route bits — verified by `make test-airplay` and `python3 scripts/smoke_airplay_mdns.py`, 2026-08-14.
- The complete host suite, discovery smoke, strict Switch trace build, and diff hygiene pass; the final NRO is 25,600,698 bytes with SHA-256 `341741d4ab3d145f7d92664b80b43c9d98b207d13f4cbe698d3dea655d580d1e` — verified locally, 2026-08-14.
- Real-device media delivery remains unverified until a new trace reaches `/play` or a type-110 mirror stream and receives media packets — explicitly retained as the next-run contract, 2026-08-14.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-14 | Step 1 | Added bounded `/audioMode` plist handling and tolerant empty `SET_PARAMETER` behavior; complete AirPlay/shared host suite passed. |
| 2026-08-14 | Step 2 | Enabled video/HLS discovery bits for the existing remote-video route; mDNS lifecycle smoke and complete host suite passed. |
| 2026-08-14 | Step 3 | Re-ran complete regression and mDNS smoke tests, produced a strict AirPlay trace NRO with staged portlibs, and recorded distinct URL-video and mirror test markers. |
| 2026-08-14 | Reflection | Re-read all changed source, tests, plan records, and receiver capability filtering; found no new correctness, ownership, security, or artifact issue. All final host tests and diff hygiene pass; real-device media remains explicitly unclaimed pending the next trace. |
