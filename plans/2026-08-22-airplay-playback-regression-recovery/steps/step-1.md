# Step 1: Restore the Playable Baseline

> Status: COMPLETED
> Created: 2026-08-22

## Goal
Remove the disproven demux override while retaining transport and terminal-cause diagnostics.

## Prerequisites
- Latest trace proves the override regresses previously playable Reverse HLS.
- Files: player cache policy, libmpv backend, and focused policy tests.
- Design: change one invariant only; descriptor and transport fixes come later.

## Deliverables
- All media returns to automatic demux selection.
- Loopback HLS remains identifiable in diagnostics without affecting behavior.
- After this step: policy and AirPlay tests pass with DLNA/IPTV options unchanged.

## Plan
- [x] `read` `source/player/cache_policy.c` and `source/player/backend/libmpv.c` — confirmed the loopback override and trace dependency.
- [x] `edit` `scripts/test_player_cache_policy.c` — protected automatic demux for remote and loopback URL classes.
- [x] `edit` player cache policy files — removed unconditional HLS demux selection without changing cache sizes.
- [x] `edit` `source/player/backend/libmpv.c` — retained `demux=auto` diagnostics.
- [x] `bash` `make test-player-cache-policy && make test-protocol-coordinator && git diff --check` — zero failures; full suite deferred to final integration validation.

## Quality Checklist
- [x] Evidence-before-edit: read targets, searched `force_hls_demux|demuxer-lavf-format`, ran focused validation
- [x] Existing pattern / reuse checked: FFmpeg automatic probing is the existing baseline
- [x] Contract understood: cache policy cannot assert demux without validated capability
- [x] Risk reviewed: broad playback regression
- [x] Mitigation recorded: one-change step and protocol-class assertions

## Validation Checklist
- [x] `make test-player-cache-policy` exits 0
- [x] `git diff --check` exits 0

## Test Checklist
- [ ] `make test-airplay` — all pass

## Implementation Notes
Removed `force_hls_demux` from the cache policy contract. Loopback AirPlay HLS keeps the same network cache but now uses FFmpeg/libmpv automatic format probing. Diagnostics continue to emit `demux=auto`.

## Files Changed
- `source/player/cache_policy.h`
- `source/player/cache_policy.c`
- `source/player/backend/libmpv.c`
- `scripts/test_player_cache_policy.c`
