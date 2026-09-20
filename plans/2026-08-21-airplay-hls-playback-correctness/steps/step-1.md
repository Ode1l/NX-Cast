# Step 1: Select FFmpeg HLS Demuxing Per AirPlay File

> Status: COMPLETED
> Created: 2026-08-21

## Goal
Ensure local AirPlay Reverse HLS URLs are decoded as HLS rather than traversed as generic M3U playlist entries.

## Prerequisites
- Latest runtime log confirms repeated per-fragment EOF/start behavior.
- Files to modify: `source/player/cache_policy.h`, `source/player/cache_policy.c`, `source/player/backend/libmpv.c`, `scripts/test_player_cache_policy.c`.
- Design: demux forcing is limited to local `/airplay-hls/` URLs.

## Deliverables
- A reusable policy predicate/formatter selects `demuxer-lavf-format=hls` only for local AirPlay HLS.
- After this step: `make test-player-cache-policy` and `make test-airplay` pass.

## Plan
- [x] `read` `source/player/cache_policy.h`, `source/player/cache_policy.c`, and `scripts/test_player_cache_policy.c` — confirm the narrow per-file option contract.
- [x] `edit` `scripts/test_player_cache_policy.c` — add protective assertions for AirPlay HLS and non-AirPlay URLs.
- [x] `edit` `source/player/cache_policy.h` and `source/player/cache_policy.c` — extend the existing formatter with the AirPlay-local HLS demux option.
- [x] `edit` `source/player/backend/libmpv.c` — no edit required because its existing 256-byte option buffer contains the 155-byte result.
- [x] `bash` `make test-player-cache-policy && make test-airplay` — zero failures.

## Quality Checklist
- [x] Evidence-before-edit: target read `libmpv.c`/`cache_policy.c`, impact search `rg "loadfile|cache_policy"`, validation `make test-player-cache-policy && make test-airplay`
- [x] Existing pattern / reuse checked: existing `player_cache_policy_format_options` is the single per-file option boundary
- [x] Contract understood: URI in, bounded comma-separated mpv options out; failure leaves load undispatched
- [x] Risk reviewed: playback regression and option buffer overflow
- [x] Mitigation recorded: exact URI-class tests, bounded `snprintf`, full AirPlay suite

## Validation Checklist
- [x] `make test-player-cache-policy` exits 0
- [x] `make test-airplay` exits 0

## Test Checklist
- [x] AirPlay local HLS includes `demuxer-lavf-format=hls`; HTTP IPTV and direct AirPlay do not.

## Implementation Notes
The protective test failed first because `force_hls_demux` did not exist. The implementation reuses `PlayerCachePolicy` and recognizes only the generated loopback path. No behavioral `libmpv.c` change was necessary in this step.

## Files Changed
- `source/player/cache_policy.h`
- `source/player/cache_policy.c`
- `scripts/test_player_cache_policy.c`
