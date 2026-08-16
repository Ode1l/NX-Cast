# Step 1: Add Media-Aware Cache Policy

> Status: COMPLETED
> Created: 2026-08-13

## Goal
Create a host-tested cache policy and apply it consistently at the libmpv load boundary while preserving low-latency AirPlay mirroring.

## Prerequisites
- User confirmed the 20 MiB common default selection.
- Files to modify: `source/player/backend/libmpv.c`, `Makefile`; new focused policy/test files under `source/player` and `scripts`.
- Existing dirty files remain untouched unless a merge is strictly required.

## Deliverables
- A pure C cache policy that classifies network media and `airplay://mirror`.
- Safe load-option formatting with 20/10 MiB and 20-second network defaults.
- Policy and buffering transition logs keyed by media sequence/hash rather than signed URL query data.
- After this step: `make test-player-cache-policy` passes.

## Plan
- [x] `write source/player/cache_policy.[ch]` — define policy classification, tunable defaults, and bounded libmpv option formatting.
- [x] `write scripts/test_player_cache_policy.c` — cover network media, mirror, null URI, and undersized buffers.
- [x] `edit source/player/backend/libmpv.c` — replace direct-MP4 special casing with the policy and emit rate-limited policy/buffering diagnostics.
- [x] `edit Makefile` — expose cache build settings and add `test-player-cache-policy` to safety/aggregate tests.
- [x] `bash make test-player-cache-policy` — all policy tests passed.

## Quality Checklist
- [x] Evidence-before-edit: target read `source/player/backend/libmpv.c`, impact search `rg cache`, validation `make test-player-cache-policy`
- [x] Existing pattern / reuse checked: existing loadfile option builder and player trace helpers reused
- [x] Contract understood: policy formats per-file options; no allocation; no raw URL logging
- [x] Risk reviewed: playback latency, memory ceiling, option truncation
- [x] Mitigation recorded: mirror-specific policy and strict formatter tests

## Validation Checklist
- [x] `make test-player-cache-policy` exits 0
- [x] `git diff --check` exits 0

## Test Checklist
- [x] Network and mirror policies, null URI, and small destination tests pass

## Implementation Notes
Added a pure cache policy at the shared libmpv boundary. Network media receives
20 MiB forward, 10 MiB backward, and a 20-second readahead ceiling; the AirPlay
mirror callback URI explicitly disables demuxer caching. Trace builds emit
policy selection and buffering enter/leave events keyed by media sequence and
URL hash. The first sandboxed test attempt could not write `build/tests`; the
approved rerun passed.

## Files Changed
- `Makefile`
- `source/player/cache_policy.c`
- `source/player/cache_policy.h`
- `source/player/backend/libmpv.c`
- `scripts/test_player_cache_policy.c`
