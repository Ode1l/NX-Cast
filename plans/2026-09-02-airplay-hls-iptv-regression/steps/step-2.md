# Step 2: Isolate Shared HLS Policies

> Status: COMPLETED
> Created: 2026-09-02

## Goal
Ensure IPTV and DLNA use stable general playback defaults while AirPlay reverse HLS receives only its required transport policy.

## Prerequisites
- Step 1 completed with a verified shared-player regression target.
- Files to modify are identified by Step 1.

## Deliverables
- Media-type-scoped transport/cache/demux options with no sender-specific branching.
- After this step: IPTV no longer inherits reverse-HLS-only behavior in host tests and source inspection.

## Plan
- [x] `edit` identified player/profile source — scope reverse-HLS options to AirPlay local manifests.
- [x] `edit` or `write` focused existing test file — protect IPTV and reverse-HLS policy separation.
- [x] `bash` discovered focused host test — expect all assertions to pass.

## Quality Checklist
- [ ] Evidence-before-edit: Step 1 target, impact search, and test command recorded.
- [ ] Existing pattern / reuse checked: extend current media policy helper rather than add parallel configuration.
- [ ] Contract understood: policy derives from media transport, not sender application.
- [ ] Risk reviewed: option leakage across sequential playback sessions.
- [ ] Mitigation recorded: explicit reset/default behavior and sequential-session test.

## Validation Checklist
- [ ] Focused host test exits 0.
- [ ] `git diff --check` exits 0.

## Test Checklist
- [ ] IPTV/direct URL policy remains default before and after AirPlay reverse-HLS playback.
- [ ] AirPlay reverse HLS receives its required nonpersistent HTTP policy.

## Implementation Notes
- Target: `source/player/cache_policy.c`, `source/player/cache_policy.h`, and the focused host test.
- Impact search: the policy formatter is consumed only by `libmpv_async_load_current`; every load receives a freshly formatted per-file option string.
- Validation: `make test-player-cache-policy` and `git diff --check`.
- Risk: a previous AirPlay load could leak explicit cache options into a later IPTV load. Mitigation: represent "do not override mpv cache" explicitly and test sequential policy selection.
- Ordinary network streams now emit only `pause=...`, allowing mpv's stable defaults. Direct MP4 uses the historical 2 second / 8 MiB / 2 MiB profile. Local reverse HLS alone retains the configurable larger cache and disables HTTP persistence.

## Files Changed
- `source/player/cache_policy.c`
- `source/player/cache_policy.h`
- `source/player/backend/libmpv.c`
- `scripts/test_player_cache_policy.c`
