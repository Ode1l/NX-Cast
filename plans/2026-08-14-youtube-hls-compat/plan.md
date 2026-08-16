# Plan: Add YouTube AirPlay HLS Compatibility

> Status: COMPLETED
> Created: 2026-08-14
> Last Updated: 2026-08-14

## Goal
Make YouTube continue from successful AirPlay pairing and FairPlay v3 setup to `/play` without expanding NX-Cast into a standalone audio receiver.

## Assumptions
- The latest YouTube trace is representative: `/fp-setup2` and `/setProperty` 501 responses are the immediate negotiation blockers.
- UxPlay's bounded compatibility behavior is the approved protocol baseline.
- Bilibili's audio-only route remains diagnostic evidence, not a request to add standalone AirPlay music playback.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] `GET /server-info` returns a dedicated HLS-oriented plist rather than the mirror `/info` body.
- [x] `POST /fp-setup2` returns HTTP 421 with the Apple binary-plist content type.
- [x] `PUT /setProperty?...` returns HTTP 200; known media properties receive `errorCode=0` and unknown properties are safely acknowledged with an empty body.
- [x] Focused AirPlay handler tests and the complete host AirPlay suite pass.

### Non-goals
- Implementing the unsupported FairPlay variant carried by `/fp-setup2`.
- Standalone AirPlay audio playback, AirPlay 2 multi-room audio, or FFmpeg muxer changes.
- Changing DLNA, IPTV, player UI, or ownership semantics.

### Edge Cases
- `/fp-setup2` bodies shorter than five bytes must not be indexed for logging.
- A malformed or empty `setProperty` property name must receive a bounded response without mutating playback state.
- Existing `/info` binary-plist behavior must remain unchanged for mirroring clients.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| FairPlay variant fallback | Return 501, fake success, or explicitly reject with 421 | Return 421 with the Apple plist content type, matching UxPlay without pretending to decrypt | yes - user approved the proposed compatibility patch |
| Property updates | Implement every property, reject unknown values, or acknowledge no-op updates | Return `errorCode=0` for known media properties and empty 200 for unknown properties | yes - user approved the proposed compatibility patch |
| Bilibili audio-only behavior | Add standalone audio playback now or retain video-player scope | Retain video-player scope and report the verified missing audio-only handoff | yes - prior AirPlay scope explicitly excludes standalone audio |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add protective handler tests and implement the YouTube HTTP compatibility contract. |
| Step 2 | `steps/step-2.md` | COMPLETED | Run regression/build checks and record the next real-device trace contract. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Focused/full host tests | `make test-airplay` | `makefile` | yes |
| Diff hygiene | `git diff --check` | Git | yes |
| Switch build | `source /opt/devkitpro/switchvars.sh && make full-trace-build BUILD_JOBS=4` | `.vscode/tasks.json`, `makefile` | if local dependencies permit |

## Context & Learnings
### Key Decisions
- Treat this as a control-plane compatibility fix because no `/play`, mirror video frame, bridge handoff, or player load occurred.
- Keep the current audio-only limitation explicit instead of coupling it to the YouTube HLS fix.

### Gotchas & Warnings
- The worktree contains extensive uncommitted AirPlay and build work that must be preserved.
- After `/play` appears, the current selected Switch FFmpeg may still expose the separately known Matroska muxer limitation.

> Append only. Never delete or rewrite existing entries below - only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260814-172144.log` | Real YouTube HTTP transcript | `sed -n '244,325p'` shows successful pairing and `/fp-setup`, then 501 for `/fp-setup2` and `/setProperty` |
| `source/protocol/airplay/protocol/handlers.c` | HTTP/RTSP route implementation | `sed` confirms `/server-info` reuses `/info` and unsupported requests fall through to 501 |
| `source/protocol/airplay/protocol/rtsp.c` | HTTP status phrase serialization | `sed` confirms status 421 currently lacks its standard reason phrase |
| `scripts/test_airplay_handlers.c` | Handler contract tests | `read` confirms existing dispatch and plist helpers can cover the new routes |
| `source/protocol/airplay/media/mirror_runtime.c` | Audio and video player handoff | `rg`/`sed` confirms audio-only SETUP cannot create a bridge or trigger player load |
| `/tmp/nxcast-airplay-ref.bLBwvR/UxPlay/lib/http_handlers.h` | Read-only GPL compatibility reference | `sed` confirms specialized `/server-info`, 421 `/fp-setup2`, and tolerant `/setProperty` behavior |

### Verified Facts
- YouTube receives 57 HTTP 501 responses and never sends `/play` in the latest trace - verified by log search, 2026-08-14.
- The receiver advertises the video/HLS feature profile, so route selection has progressed beyond discovery - verified by current integration code and the YouTube HTTP transcript, 2026-08-14.
- Bilibili negotiates a type-96 ALAC stream, but `airplay_mirror_runtime_record()` requires an existing mirror and bridge before loading the player - verified by `mirror_runtime.c`, 2026-08-14.
- UxPlay deliberately rejects `/fp-setup2` with 421 and no decryption while still allowing HLS negotiation to continue - verified from the local read-only upstream clone, 2026-08-14.
- The compatibility transcript failed with 13 expected assertions before implementation and the complete `make test-airplay` suite passed afterward - verified by consecutive test runs, 2026-08-14.
- The Full Trace Switch build embeds profile `full-owner-exclusive-observe-bsd12` and the new compatibility markers - verified with `strings NX-Cast.nro`, 2026-08-14.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-14 | Step 1 | Added dedicated HLS server info, safe 421 FairPlay fallback, tolerant property acknowledgements, and exact handler coverage. |
| 2026-08-14 | Step 2 | Re-ran the complete host suite, passed diff hygiene, and built a 25,600,698-byte Full Trace NRO with SHA-256 `f97d8a787b502af33ff7eb47340357c24ede837508e8d37317de0ca6a68ea31a`. |
| 2026-08-14 | Reflection | Re-read the plan, steps, source, and tests; replaced the raw HLS feature value with a named protocol constant, then re-ran the complete host suite, Full Trace build, artifact inspection, and diff hygiene successfully. |
