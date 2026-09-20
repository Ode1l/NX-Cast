# Step 2: Repair Verified Regressions

> Status: COMPLETED
> Created: 2026-09-02

## Goal
Implement the smallest generic repairs justified by Step 1 and validate them across AirPlay and the Switch target.

## Prerequisites
- Step 1 completed with exact causal behavior, target files, and test contracts.
- No sender-specific branch is required.

## Deliverables
- Focused regression tests and corresponding generic implementation fixes.
- After this step: a Full Trace Switch build is ready for another mixed YouTube, Bilibili, and IPTV run.

## Plan
- [x] `edit` `scripts/test_airplay_remote_video.c` and `scripts/test_airplay_remote_hls.c` — require active-session detach notification without stop and connection closure for local playlists.
- [x] `edit` `source/protocol/airplay/media/remote_video.{c,h}`, `source/protocol/airplay/media/remote_hls.c`, and `source/protocol/airplay/integration.c` — implement the minimum generic protocol/media correction.
- [x] `bash` focused tests and `make test-airplay` — verify regression and aggregate AirPlay behavior.
- [x] `bash` `make dev-build BUILD_JOBS=4` and `git diff --check` — verify Switch build and patch hygiene.

## Quality Checklist
- [x] Evidence-before-edit: target and all direct callers read; impact search complete; validation command confirmed.
- [x] Existing pattern / reuse checked: existing lifecycle/state helpers preferred over new abstractions.
- [x] Contract understood: retry, replacement, ownership, transport close, and media stop semantics documented.
- [x] Risk reviewed: AirPlay, DLNA, IPTV, player lifecycle, and hardware decode regressions.
- [x] Mitigation recorded: focused tests, full AirPlay suite, and Switch build.

## Validation Checklist
- [x] Focused tests exit 0.
- [x] `make test-airplay` exits 0 when applicable.
- [x] `make dev-build BUILD_JOBS=4` exits 0.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] Retry/replacement and media-state cases identified in Step 1 pass.

## Implementation Notes
Added explicit control attach/detach callbacks to the remote-video boundary. Final logical control close now updates the protocol media session to detached without stopping or releasing playback; a later request from the same logical session restores attached state. Local reverse-HLS playlist responses now close their HTTP connection after each response so loopback workers do not occupy scarce BSD sessions throughout playback. No sender-specific, codec, cache, or TLS policy was introduced.

## Files Changed
- `source/protocol/airplay/media/remote_video.h`
- `source/protocol/airplay/media/remote_video.c`
- `source/protocol/airplay/media/remote_hls.c`
- `source/protocol/airplay/integration.c`
- `scripts/test_airplay_remote_video.c`
- `scripts/test_airplay_remote_hls.c`
- `plans/2026-09-02-media-regression-triage/plan.md`
- `plans/2026-09-02-media-regression-triage/steps/step-1.md`
- `plans/2026-09-02-media-regression-triage/steps/step-2.md`
