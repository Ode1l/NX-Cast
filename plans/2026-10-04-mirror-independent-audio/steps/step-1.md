# Step 1: Independent audio runtime

> Status: COMPLETED
> Created: 2026-10-04

## Goal
Feed mirror audio independently with the same media origin and no video restart.
## Prerequisites
- Verified first audio arrives without another IDR.
- Existing retained stream bridge and runtime worker.
## Deliverables
- Audio bridge lifecycle and callback, video timeline transfer, focused regression.
## Plan
- [x] Edit clock/bridge to transfer video timeline to audio-only container.
- [x] Edit runtime to queue audio attach after actual muxed audio, remove IDR dependency.
- [x] Update black-box runtime tests for audio removal/re-setup and no-IDR activation.
- [x] Run make test-airplay.
## Quality Checklist
- [x] Targets read, consumers searched, validation commands identified.
- [x] Existing worker/clock reused; no RTP callback player calls.
- [x] Cancel before join; retain buffers across callbacks.
## Validation Checklist
- [x] git diff --check passes (confirmed before next step).
## Test Checklist
- [x] make test-airplay passes.
## Implementation Notes
Host suite passed twice. Real audio packets activate and re-activate without new video packets, video generation/reload/stop counts unchanged. A four-second late audio origin maps to 360000 ticks, not zero. Hardware acceptance remains pending.
## Files Changed
source/protocol/airplay/mirror/clock.{c,h}; source/protocol/airplay/media/stream_bridge.{c,h}; source/protocol/airplay/media/mirror_runtime.{c,h}; scripts/test_airplay_clock.c; scripts/test_airplay_mirror_runtime.c.
