# Step 2: Coherent Media Selection and Repeat Playback

> Status: COMPLETED
> Created: 2026-09-01

## Goal
Prepare only a coherent generic set of HLS media playlists and preserve clean cancellation, replacement, and ownership behavior across repeated plays.

## Prerequisites
- Step 1 completed with length-safe playlist transformation.
- Files to modify: `source/protocol/airplay/media/remote_hls.c`, `source/protocol/airplay/media/remote_video.c`, `scripts/test_airplay_remote_hls.c`, `scripts/test_airplay_remote_video.c` as required by observed call impact.
- Design confirmed: selection derives from HLS attributes and receiver capabilities, never sender identity.

## Deliverables
- Rewritten master and FCUP request list remain exactly consistent and avoid fetching unrelated alternate media groups.
- Repeated `/play`, `/stop`, and replacement transactions release HLS state and player ownership exactly once.
- After this step: AirPlay host suite and Switch development build pass.

## Plan
- [x] `edit` `scripts/test_airplay_remote_hls.c` and `scripts/test_airplay_remote_video.c` — add multi-variant/audio-group and repeat-play transcript coverage.
- [x] `edit` `source/protocol/airplay/media/remote_hls.c` — select and register a coherent generic media set while emitting a master that references exactly that set.
- [x] `edit` `source/protocol/airplay/media/remote_video.c` only if tests expose transaction cleanup duplication — no production change was needed because repeat-play ownership already balanced correctly.
- [x] `bash` `make test-airplay` — verify focused HLS, remote-video, session, coordinator, DLNA, IPTV, and aggregate behavior.
- [x] `bash` `make dev-build BUILD_JOBS=4` — verify the Switch target links with the repaired protocol path.

## Quality Checklist
- [x] Evidence-before-edit: target/callers read, `rg` impact search complete, validation commands identified.
- [x] Existing pattern / reuse checked: extended the current HLS registry and generation model; added no state machine or dependency.
- [x] Contract understood: local master references only stored media; ownership starts at `READY`; stop/replacement invalidates one generation atomically.
- [x] Risk reviewed: selecting incompatible tracks, stale reverse responses, double release, cross-protocol regression.
- [x] Mitigation recorded: attribute-driven selection, generation checks, transcript tests, coordinator regression tests.

## Validation Checklist
- [x] Focused and aggregate AirPlay host tests exit 0.
- [x] `make dev-build BUILD_JOBS=4` exits 0.

## Test Checklist
- [x] Multi-variant master fetches only retained media and serves no dangling local URI.
- [x] Stop followed by a new `/play` reaches ready and balances owner claim/release.
- [x] Existing protocol coordinator tests remain green.

## Implementation Notes
The master rewrite now selects the highest-bandwidth stream variant and the default audio rendition from its referenced group, then registers only those local playlists. This removes the previous eager prefetch of every alternate rendition while keeping selection based on HLS metadata rather than sender identity. The existing `remote_video` ownership implementation passed a second complete reverse-HLS transaction, so no production lifecycle code was changed.

## Files Changed
- `source/protocol/airplay/media/remote_hls.c`
- `scripts/test_airplay_remote_hls.c`
- `scripts/test_airplay_remote_video.c`
