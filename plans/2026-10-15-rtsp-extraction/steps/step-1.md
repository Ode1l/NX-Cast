# Step 1: Extract The RTSP Request Parser

> Status: COMPLETED
> Created: 2026-10-15

## Goal
Make the existing request parser reusable without changing AirPlay behavior.

## Prerequisites
- Clean main at 0efdba4; existing parser/header/test and build recipes inspected.
- User approved refactoring before new protocol implementation.

## Deliverables
- Common request types/parser with compatibility wrappers for existing callers.
- Updated host and Switch source lists, validation results and device test instructions.

## Plan
- [x] Preserve previous artifacts and run baseline RTSP tests.
- [x] Extract parser and shared private syntax helpers; keep AirPlay response/session logic.
- [x] Update build inputs and run regression tests and Switch build.
- [x] Record results and device handoff.

## Quality Checklist
- [x] Evidence-before-edit: read rtsp.c/h and test_airplay_rtsp.c; searched makefile callers.
- [x] Existing reuse: MediaActor and session state machine remain existing shared facilities.
- [x] Contract: identical request layout, limits, results and body lifetime.
- [x] Risk: accidental parse or build behavior changes.
- [x] Mitigation: mechanical extraction, original tests and original algorithm comparison.

## Validation Checklist
- [x] git diff --check
- [x] Switch build succeeds with matching ELF/NRO.

## Test Checklist
- [x] Baseline and extracted parser pass existing RTSP tests; extracted parser also passes ASan/UBSan.
- [x] Complete make test-airplay suite passes, including pairing and receiver socket smoke tests.

## Implementation Notes
Moved request structures and parsing into protocol/rtsp; the old AirPlay API delegates through three wrappers. Shared syntax helpers are private static inline functions; parser-only helpers remain in the parser C file. No new allocation, thread, queue or lock was introduced. Original response encoding and dispatch code remains verbatim. Existing parser tests were reused rather than duplicated.

Previous NRO/ELF preserved at /tmp/nxcast-before-rtsp-20261015 (their exact source revision is unverified). The source baseline is Git commit 0efdba4281122cdd97de0c3c1a7ebde7485d6d15.

Validation logs:
- /tmp/nxcast-rtsp-host-tests-20261015.log
- /tmp/nxcast-rtsp-switch-build-20261015.log

Normal build, version 0.3.2, AirPlay enabled, traces disabled, pinned FFmpeg 7.1-4:
- NX-Cast.nro SHA256: cebd652df91ed4ce3342cd6050b951ea2541eb28af46f809957efc8c65aaf106
- NX-Cast.elf SHA256: e994054fe6859a8cf5d236bc7a930b13d4aebb7afd4d5c87e2c38a92937ad17d

### Device Handoff (Pending)
Use VS Code `NX-Cast: Upload Only` for the existing build. Keep the current SD assets and pairing data. Run the same media/senders used for the working baseline:
1. DLNA: play, pause, seek, stop, then cast a second video.
2. IPTV: open a channel, switch several times, return Home.
3. AirPlay app video: play, seek, switch videos, disconnect and reconnect. Bilibili and YouTube should be checked separately.
4. AirPlay screen mirroring: verify sound, rotate the phone, stop and reconnect.
5. Switch from AirPlay back to DLNA/IPTV and exit the application.

If a regression occurs, use `NX-Cast: Full Trace & Upload + Logs`, retain that build's matching ELF, and report the exact action and sender. Completion here covers local extraction/validation; do not proceed to the next refactoring slice until device behavior is confirmed.

## Files Changed
- plans/2026-10-15-rtsp-extraction/plan.md
- plans/2026-10-15-rtsp-extraction/steps/step-1.md
- source/protocol/rtsp/request.h
- source/protocol/rtsp/rtsp_request.c
- source/protocol/rtsp/syntax_internal.h
- source/protocol/airplay/protocol/rtsp.h
- source/protocol/airplay/protocol/rtsp.c
- makefile
