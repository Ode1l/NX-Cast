# Step 4: Verify Integrated Playback Boundaries

> Status: COMPLETED
> Created: 2026-09-02

## Goal
Validate the repaired policy and state boundaries and define a minimal device test that distinguishes remaining protocol and media failures.

## Prerequisites
- Steps 2 and 3 completed.
- Repository test/build commands discovered.

## Deliverables
- Passing focused tests and available build checks.
- After this step: device testing has exact expected log markers for DLNA, IPTV, AirPlay direct URL, and reverse HLS.

## Plan
- [x] `bash` repository host tests — run the narrowest complete relevant suite.
- [x] `bash` Switch compile command — verify source and linkage when local dependencies permit.
- [x] `bash` `git diff --check` and bounded diff review — verify surgical scope.
- [x] `edit` plan files — record validation, residual risks, and device-test markers.

## Quality Checklist
- [ ] Evidence-before-edit: implementation diffs and test targets reviewed.
- [ ] Existing pattern / reuse checked: no duplicate state or policy framework introduced.
- [ ] Contract understood: independent receivers, coordinator ownership, serialized media actor.
- [ ] Risk reviewed: device-only FFmpeg/nvtegra behavior remains outside host tests.
- [ ] Mitigation recorded: one deterministic mixed-protocol device sequence.

## Validation Checklist
- [ ] Relevant host tests pass.
- [ ] Switch compile passes or dependency blocker is reported exactly.
- [ ] `git diff --check` exits 0.

## Test Checklist
- [ ] Device sequence: stable DLNA, known-good IPTV, AirPlay first play, replacement play, long seek, teardown.

## Implementation Notes
- `make test-airplay` exited 0, including remote HLS, remote video, logical session, coordinator, actor, and cache-policy tests.
- `make -j4` exited 0 and produced `NX-Cast.nro`.
- `git diff --check` exited 0. The worktree contains the broader ongoing AirPlay branch changes; this step reviewed the task paths without reverting unrelated user work.
- Device retest sequence: play DLNA; play the known IPTV live URL for at least 60 seconds; AirPlay YouTube first play; long seek; second play; Bilibili first and replacement play; explicit stop. Expected cache markers are `network-default` for IPTV, `direct-mp4-fast` for direct video, and `airplay-reverse-hls` with `http_persistent=0` for reverse HLS.
- Residual risk: host tests cannot validate nvtegra decode output or sender-provided live CDN timing on Switch hardware.

## Files Changed
- `plans/2026-09-02-airplay-hls-iptv-regression/plan.md`
- `plans/2026-09-02-airplay-hls-iptv-regression/steps/step-1.md`
- `plans/2026-09-02-airplay-hls-iptv-regression/steps/step-2.md`
- `plans/2026-09-02-airplay-hls-iptv-regression/steps/step-3.md`
- `plans/2026-09-02-airplay-hls-iptv-regression/steps/step-4.md`
