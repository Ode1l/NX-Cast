# Step 2: Audio readiness and diagnostics

> Status: COMPLETED
> Created: 2026-10-04

## Goal
Avoid activating an empty audio track and provide evidence of packet delivery.
## Prerequisites
- Step 1 passes protocol regressions.
## Deliverables
- First-audio-frame gate, bounded receive summaries and Full Trace binary.
## Plan
- [x] Gate late-track activation on a real audio frame and a video IDR.
- [x] Log first UDP packets, failures and final counters without sensitive payloads.
- [x] Update focused runtime regression and run host tests.
- [x] Compile with staged revision 4 and preserve matching ELF.
## Quality Checklist
- [x] Inspect audio worker and runtime callback locking.
- [x] No extra threads or app-specific policies.
- [x] No per-frame log flood.
## Validation Checklist
- [x] Device build succeeds; map uses revision 4.
- [x] git diff --check passes.
## Test Checklist
- [x] make test-airplay and make test-ui pass.
## Implementation Notes
Video containers remain video-only until a real audio frame and IDR arrive. Only
one newest compressed frame is buffered and seeded into the replacement container.
Audio removal clears pending data, ignores closing-worker callbacks, and leaves
video alive; removing an already active track creates a video-only replacement at
the next IDR. Full stop cancels both containers before joining workers.

Packet diagnostics log first data/control receive, first rejection, first delivery,
and counters after join. No payloads or keys are logged. Regression covers no-audio
IDR, real audio activation, active audio removal, pre-activation removal, re-SETUP,
and video removal with audio retained. Host suites exited 0; Full Trace build exited
0 and build/NX-Cast.map LOAD entries select staged revision-4 FFmpeg.

Binary: artifacts/NX-Cast-stream-teardown-full-trace.nro; matching ELF:
logs/NX-Cast-stream-teardown.elf. Build ID:
b3b980f9e100d2b74dd7a3ebf2b812fa85ca41b4.
Hardware verification remains separate from host validation. User indicated latest
log may not have been saved: rechecked logs and runtime logs; newest nxlink file
still modified 2026-10-04 00:14:42, 476529 bytes. Requested its replacement/name;
do not attribute the unrecorded run to the old log.
## Files Changed
- source/protocol/airplay/media/mirror_runtime.c
- source/protocol/airplay/media/stream_bridge.c
- source/protocol/airplay/media/stream_bridge.h
- source/protocol/airplay/mirror/audio.c
- scripts/test_airplay_mirror_runtime.c
