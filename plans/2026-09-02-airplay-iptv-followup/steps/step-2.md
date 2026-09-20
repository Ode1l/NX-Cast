# Step 2: Repair Verified Follow-up Regressions

> Status: COMPLETED
> Created: 2026-09-02

## Goal
Implement the smallest generic repairs justified by Step 1 and validate them across AirPlay and the Switch target.

## Prerequisites
- Step 1 is completed with exact causal behavior, source targets, and test contracts.
- No sender-specific branch is required.

## Deliverables
- Focused regression tests and implementation fixes for receiver-controlled failures.
- A trace-capable Switch build ready for the next mixed-media run.

## Plan
- [x] `edit` focused tests selected in Step 1 — reproduce each verified regression.
- [x] `edit` selected source files — implement minimum generic corrections.
- [x] `bash` focused tests and `make test-airplay` — verify AirPlay behavior.
- [x] `bash` `make dev-build BUILD_JOBS=4` and `git diff --check` — verify target build and hygiene.

## Quality Checklist
- [ ] Evidence-before-edit: targets and callers read; impact search and validation confirmed.
- [ ] Existing pattern / reuse checked: current lifecycle and transport helpers preferred.
- [ ] Contract understood: retry, seek, ownership, connection, and media-stop semantics documented.
- [ ] Risk reviewed: AirPlay, DLNA, IPTV, network, and hardware-decode regressions.
- [ ] Mitigation recorded: focused tests, AirPlay suite, Switch build, and explicit residual risks.

## Validation Checklist
- [ ] Focused tests exit 0.
- [ ] `make test-airplay` exits 0 when applicable.
- [ ] `make dev-build BUILD_JOBS=4` exits 0.
- [ ] `git diff --check` exits 0.

## Test Checklist
- [ ] Retry, seek, connection, and media-state cases identified in Step 1 pass.

## Implementation Notes
The player cache policy recognizes only loopback `/airplay-hls/` URLs and adds `demuxer-lavf-o=http_persistent=no`, preventing stale cross-host HLS connection reuse after seeks. The reverse-HLS master rewrite scans explicit `CODECS` attributes and, when H.264 is available, omits explicitly VP9/AV1/HEVC variants while preserving audio and unknown-codec fallback entries. Media-cache traces report the selected persistence setting.

## Files Changed
- `source/player/cache_policy.c`
- `source/player/cache_policy.h`
- `source/player/backend/libmpv.c`
- `source/protocol/airplay/media/remote_hls.c`
- `scripts/test_player_cache_policy.c`
- `scripts/test_airplay_remote_hls.c`
