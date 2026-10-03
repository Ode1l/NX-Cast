# Step 1: Normalize negotiated NTP audio clock

> Status: COMPLETED
> Created: 2026-10-04

## Goal
Route NTP audio timestamps into the video time domain and log the pre-mux drop reason.

## Prerequisites
- Latest trace and local UxPlay comparison read.
- Previous independent audio backend compiled and host suite passed.

## Deliverables
- A failing-then-passing NTP audio activation/re-SETUP regression.
- Normalized transport-boundary sync and bounded diagnostic evidence.
- Full Trace NRO and matching ELF using corrected FFmpeg.

## Plan
- [x] Edit scripts/test_airplay_mirror_runtime.c: add realistic NTP epoch test, demonstrate failure.
- [x] Edit mirror_runtime.c: preserve negotiated mode and normalize audio sync only for NTP.
- [x] Edit clock diagnostics: expose mapped skew and missing-sync drops even before first muxed packet.
- [x] Run host tests, diff review and device build; archive binary.

## Quality Checklist
- [x] Evidence-before-edit: runtime/audio/clock read; callers searched; make test-airplay discovered.
- [x] Reuse checked: existing clock stats and runtime observer used.
- [x] Contract: Q32.32 arithmetic, preserve fractions and era wrap, no changes to raw RTP parser callbacks.
- [x] Risk reviewed: timeline mismatch and logging pressure.
- [x] Mitigation: NTP/no-NTP regressions, rate-limited diagnostics, video path untouched.

## Validation Checklist
- [x] git diff --check and Full Trace build exit 0.

## Test Checklist
- [x] NTP regression fails before fix, passes after fix.
- [x] make test-airplay passes, including fractional timestamps and NTP era wrap.

## Implementation Notes
- RED: make test-airplay, exit 2, /tmp/nxcast-audio-epoch-red.log. Five runtime checks failed because an NTP audio track never attached; existing no-NTP cases passed.
- GREEN: make test-airplay, exit 0, /tmp/nxcast-audio-epoch-final-tests.log. First attach, audio-only removal/re-SETUP, no video reload, Q32.32 half-second precision, era wrap and existing suite passed.
- Build: make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1 with PORTLIBS="/Users/ode1l/Documents/VSCode/NX-Cast/artifacts/toolchain/ffmpeg/revision-4/opt/devkitpro/portlibs/switch /opt/devkitpro/portlibs/switch", exit 0, /tmp/nxcast-audio-epoch-build.log. No compiler warnings; link map confirms revision-4 libavformat/libavcodec/libavutil.
- Review: target sections re-read, clock stats callers searched, cumulative diff reviewed without reverting prior work; git diff --check exit 0.
- Binary: artifacts/NX-Cast-audio-epoch-full-trace.nro (same as root NX-Cast.nro); SHA256 b5817d469aaa9d449eca8a4cd0800477f007d0241b1c4019d5468eacf58816a5.
- Matching ELF: logs/NX-Cast-audio-epoch.elf; Build ID 5a0d1ef627d48fdd49635d3d5c15cb27f261cb19.
- Unknown: latest device trace had no raw clock values, so this proven contract bug is not yet confirmed as the only device failure. Host audio fixture is AAC-LC; real AAC-ELD decoding, output and synchronization remain hardware acceptance.
- Next device run: Upload + nxlink server only (no default Rebuild against old global FFmpeg), test Bilibili/Xiaohongshu mirror audio for at least ten seconds and after switching/stopping/reconnecting. If silent, sync-clock/bridge/audio-add/select-track/decoder/output logs distinguish the remaining stage.

## Files Changed
- source/protocol/airplay/media/mirror_runtime.c: negotiated timing mode, NTP delta normalization, bounded pre-mux drop diagnostics.
- source/protocol/airplay/mirror/clock.c: capture rejected candidate PTS and expose current clock readiness/anchors in stats.
- source/protocol/airplay/mirror/clock.h: diagnostic clock stats fields (not wire format).
- scripts/test_airplay_mirror_runtime.c: NTP/non-NTP audio activation and re-SETUP regression with fractional/era-wrap assertions.
- plans/2026-10-04-mirror-audio-epoch/plan.md and steps/step-1.md: evidence and validation record.
