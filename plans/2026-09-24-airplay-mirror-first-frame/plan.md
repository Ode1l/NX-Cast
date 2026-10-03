# Plan: Diagnose AirPlay mirror first-frame failure

> Status: COMPLETED
> Created: 2026-09-24
> Last Updated: 2026-09-24

## Goal
Allow late audio SETUP during active mirroring and make first invalid video access units diagnosable.

## Assumptions
- The invalid first access unit may be caused by crypto or NAL framing; current logs cannot distinguish them.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] Audio SETUP after mirror RECORD does not stop the video session in host tests.
- [x] A rejected first video packet logs bounded, non-secret framing metadata.
- [x] Existing AirPlay tests and Switch build pass.
### Non-goals
- Do not change key derivation, AES algorithm, or video framing without evidence.
### Edge Cases
- Audio-only RECORD must retain its behavior; wrong session must still be rejected.

## Design Decisions
None — no design-sensitive changes.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Accept late audio and diagnose first invalid video packet |
| Step 2 | `steps/step-2.md` | COMPLETED | Correct shared-secret media key derivation using reference vectors |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Host tests | `make test-airplay` | `makefile` | yes |
| Switch build | `make RELEASE_JOBS=4 release-build` | `makefile` | yes |
| Diagnostic build | `make full-trace-build BUILD_JOBS=4` | `makefile` | yes for step 2 |
| Hygiene | `git diff --check` | Git | yes |

## Context & Learnings
### Key Decisions
- Preserve media protocol behavior except the demonstrated late-audio rejection; log framing data only on invalid packets.
### Gotchas & Warnings
- Preserve unrelated README and existing AirPlay edits from the prior turn.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260924-163721.log` | Two real-device retries | Config succeeds, first access unit invalid, late audio SETUP returns 461 |
| `source/protocol/airplay/media/mirror_runtime.c` | Audio RECORD callback | Read callback constraints and audio-open behavior |
| `source/protocol/airplay/mirror/mirror_session.c` | Invalid AU boundary | Read packet decrypt and parse path |
### Verified Facts
- Both attempts accept config, decrypt one video packet, reject AU, and see no keyframe, verified from latest log.
- Late audio SETUP calls audio RECORD callback; it rejects active mirror sessions, verified from handlers and runtime source.
- UxPlay expects a four-byte big-endian NAL length in decrypted video; current parser does too, verified from local reference and `video.c`.
- Both references hash the 16-byte unwrapped key followed by the 32-byte pairing secret with SHA-512 and retain 16 bytes. NX-Cast uses SHA-256. UxPlay's debug text incorrectly calls this SHA-256, but its crypto implementation selects EVP_sha512.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-24 | Step 1 | Late video-audio RECORD accepted, invalid AU framing logged, host AirPlay tests and release build passed. Real-device first-frame result remains unverified. |
| 2026-09-24 | Step 2 | Fixed SHA-256 to SHA-512 paired media derivation. Independent vector failed both prepared/opened key checks before fix; full AirPlay tests and Full Trace build passed after fix. Device playback pending. |
