# Step 4: Validate HLS Normalization Before Playback

> Status: COMPLETED
> Created: 2026-08-22

## Goal
Preserve declared HLS structure while normalizing only explicitly declared condensed URLs, then leave byte-level demux selection to FFmpeg.

## Prerequisites
- Step 3 completed with stable session authority.
- Files: Reverse HLS parser, player load policy, and HLS/policy tests.
- Design: behavior derives only from protocol tags and media structure, never sender or host identity.

## Deliverables
- Master topology and audio renditions remain intact.
- Standard `EXT-X-MAP` initialization URIs and condensed zero-based initialization ranges remain intact.
- Demux selection stays automatic and never depends on URL host or sender identity.

## Plan
- [x] `read` HLS parser and player load boundary — documented the normalization and demux contracts.
- [x] `read` the local UxPlay implementation — confirmed condensed expansion preserves the first resource instead of synthesizing `EXT-X-MAP`.
- [x] `edit` HLS tests — covered audio, standard `EXT-X-MAP`, and a declared condensed zero-based initialization range.
- [x] `edit` player load policy — restored automatic demux selection for all network HLS inputs.
- [x] `bash` `make test-player-cache-policy && make test-airplay && git diff --check` — all checks passed.

## Quality Checklist
- [x] Evidence-before-edit: traced the first missing-initialization error back to an earlier local HTTP 503
- [x] Existing pattern / reuse checked: compared current normalization with local UxPlay source
- [x] Contract understood: the proxy rewrites locations; FFmpeg validates media bytes
- [x] Risk reviewed: synthetic map insertion could corrupt valid condensed playlists
- [x] Mitigation recorded: preserve declared ordering and test the zero-based first range

## Validation Checklist
- [x] `make test-player-cache-policy` exits 0
- [x] `git diff --check` exits 0

## Test Checklist
- [x] `make test-airplay` — all normalization fixtures pass

## Implementation Notes
The earlier plan assumed the fMP4 descriptor itself was malformed. Runtime ordering disproved that: FFmpeg's first local reads received 503, then later fragments were parsed without their zero-based initialization resource. The implementation therefore does not synthesize tags or force a demuxer. It preserves the declared first range, isolates its transport capacity in Step 2, and lets FFmpeg inspect the resulting bytes.

## Files Changed
- `source/player/cache_policy.h`
- `source/player/cache_policy.c`
- `source/player/backend/libmpv.c`
- `scripts/test_player_cache_policy.c`
- `scripts/test_airplay_remote_hls.c`
