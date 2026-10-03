# Step 2: Player audio attachment

> Status: COMPLETED
> Created: 2026-10-04

## Goal
Attach independent mirror audio to the existing mpv instance using the media actor.
## Prerequisites
- Step 1 runtime callback and no-IDR regression pass.
## Deliverables
- Ownership-gated audio command, cancellable unique stream URI, Full Trace binary.
## Plan
- [x] Wire runtime audio callback through integration and actor retained payload.
- [x] Add asynchronous external audio attachment/removal in libmpv.
- [x] Preserve mirror timestamps and diagnose attach replies/audio output.
- [x] Run host tests and staged revision-4 device build; archive matching ELF.
## Quality Checklist
- [x] Targets read, consumers searched, validation commands identified.
- [x] No new player instance or protocol-specific app filters.
- [x] Async replies and URI validated against current audio generation (code review; native runtime needs device).
## Validation Checklist
- [x] Full Trace build and git diff --check pass.
## Test Checklist
- [x] make test-airplay and make test-ui pass.
## Implementation Notes
Validated final host protocol suite, UI suite, device build and diff check, all exit 0. No compiler warnings. Logs: /tmp/nxcast-independent-audio-tests.log, /tmp/nxcast-independent-audio-ui-tests.log, /tmp/nxcast-independent-audio-build.log.

Native audio-add uses auto, selects only after a current-generation reply, removes stale external tracks by their unique URI, aborts and cancels before removal. Mirror-only rebase-start-time=no preserves video origin for late audio. Audio pending before FILE_LOADED waits; leaving mirror clears audio. Audio-add failure cancels its producer buffer without changing video state.

Audio takes a video timeline snapshot; video never waits on the audio mux mutex. Removed unused IDR replacement helper. Promotion allocates a fresh audio container rather than reusing a possibly cancelled reader. Progress logged at first and every 256 muxed packets; failures bounded. Decoder-ready observes sample rate, not a claim of audible hos output.

Final NRO: artifacts/NX-Cast-independent-audio-full-trace.nro (same as root NX-Cast.nro).
SHA256: 9824c9242d8dcae071af7c38808c2ee0d091f8cf367e3b355604cc242157be3d.
Matching ELF: logs/NX-Cast-independent-audio.elf, Build ID 3183acc72c8e76f887c4cbf82bee2fda9560175e.
Link map confirms revision-4 libavformat/libavcodec/libavutil, not global revision 3.

Hardware acceptance pending: use NX-Cast: Upload + nxlink server (no rebuild), enable sound, mirror video for 10 seconds, switch video, disconnect/reconnect. Check sound and lip sync; retain Full Trace if silent. Standalone audio remains the agreed log-only stub.
## Files Changed
source/player/player.h; source/player/core/session.c; source/player/backend/libmpv.c; source/player/backend/libmpv_airplay.h; source/protocol/airplay/integration.c; source/protocol/airplay/media/mirror_runtime.c; source/protocol/airplay/media/stream_bridge.{c,h}. Binaries archived locally, not committed.
