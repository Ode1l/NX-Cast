# Step 1: Implement ALAC Matroska Bridge

> Status: COMPLETED
> Created: 2026-08-13

## Goal
Add ct=2 format negotiation and prove that AAC and ALAC streams are described correctly in the Matroska bridge.

## Prerequisites
- User confirmed reuse of FFmpeg ALAC decoding and the proposed bridge route.
- Files to modify: `source/protocol/airplay/mirror/audio.h`, `source/protocol/airplay/mirror/audio.c`, `source/protocol/airplay/media/stream_bridge.c`, and focused AirPlay tests.
- Existing host FFmpeg provides the Matroska muxer and ALAC decoder.

## Deliverables
- ALAC magic-cookie construction for 44.1-kHz stereo AirPlay audio.
- Codec-aware bridge output using Matroska.
- After this step: focused AirPlay audio and stream-bridge host tests pass.

## Plan
- [x] `edit` `source/protocol/airplay/mirror/audio.h` and `audio.c` — add ct=2 and a bounded ALAC codec configuration.
- [x] `edit` `source/protocol/airplay/media/stream_bridge.c` — select Matroska and map the negotiated codec id.
- [x] `edit` focused AirPlay tests — cover ALAC metadata, unsupported formats, runtime negotiation, and demuxed stream codec ids.
- [x] `bash` `make test-airplay` — all AirPlay host tests pass.

## Quality Checklist
- [x] Evidence-before-edit: targets read, callers found with `rg`, validation is `make test-airplay`.
- [x] Existing pattern / reuse checked: existing `AirPlayMirrorAudioFormat`, FFmpeg H.264 parser, and stream bridge are extended rather than replaced.
- [x] Contract understood: RTP payload remains encrypted/decrypted unchanged; codec metadata, H.264 framing metadata, and container output change.
- [x] Risk reviewed: buffer bounds, muxer latency, codec metadata, time-base conversion, and AAC regression.
- [x] Mitigation recorded: fixed-size cookie, explicit codec id, padded parser input, AAC+ALAC demux tests, unsupported-format tests, and full AirPlay suite.

## Validation Checklist
- [x] Host compiler reports no warnings under existing `HOST_CFLAGS`.
- [x] Generated Matroska artifacts reopen as H.264/AAC and H.264/ALAC with 36-byte ALAC extradata.

## Test Checklist
- [x] `make test-airplay` — all pass.

## Implementation Notes
- Added AirPlay `ct=2` as ALAC with the canonical 36-byte QuickTime magic cookie for 44.1-kHz stereo and 352 samples/frame.
- Replaced MPEG-TS bridge output with non-seekable Matroska and rescaled the 90-kHz mirror clock to the muxer-selected stream time bases.
- Matroska requires H.264 dimensions and CodecPrivate before its header. The bridge now reuses FFmpeg's H.264 parser on the first keyframe and supplies padded Annex-B SPS/PPS data as extradata; no resolution is hard-coded.
- Audio received before the first video keyframe is intentionally ignored until Matroska video metadata is available. Standalone audio remains a non-goal.
- Updated the remote HLS smoke to remux the generated AAC Matroska fixture to MPEG-TS, keeping the HLS test independent of the internal AirPlay bridge container.
- Scope deviation: `scripts/test_airplay_mirror_runtime.c` and `scripts/smoke_airplay.py` were direct test-contract dependencies and were updated after the complete suite exposed them.

## Files Changed
- `source/protocol/airplay/mirror/audio.h`
- `source/protocol/airplay/mirror/audio.c`
- `source/protocol/airplay/media/stream_bridge.c`
- `scripts/test_airplay_audio.c`
- `scripts/test_airplay_stream_bridge.c`
- `scripts/test_airplay_mirror_runtime.c`
- `scripts/smoke_airplay.py`
