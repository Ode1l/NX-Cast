# Step 2: Bound Detached Media And Replacement

> Status: COMPLETED
> Created: 2026-08-22

## Goal
Give detached media a bounded lease and make every new `/play` atomically replace the previous generation.

## Prerequisites
- Step 1 completed with sender sub-connections associated.
- Files to modify: remote video lifecycle, AirPlay integration polling, coordinator event adapters, and tests.
- Design: explicit intent stops immediately; incidental close gets a short generic grace period.

## Deliverables
- Detached active media cannot persist indefinitely without sender control.
- New `/play` invalidates old generation before Stop/release/load effects.
- After this step: replacement and detached timeout black-box tests pass.

## Plan
- [x] `edit` `scripts/test_airplay_remote_video.c` — add detached timeout, reconnect/replacement, explicit Stop, and stale-generation cases.
- [x] `edit` `source/protocol/airplay/media/remote_video.h/.c` — add monotonic polling and bounded detach deadline.
- [x] `edit` `source/protocol/airplay/integration.h/.c` and `source/main.c` — poll lifecycle from the main loop without blocking.
- [x] `edit` coordinator adapters only if required — no adapter change was required; existing generation reducer remains authoritative.
- [x] `bash` focused remote-video test, `make test-protocol-coordinator`, and `git diff --check` — zero failures.

## Quality Checklist
- [x] Evidence-before-edit: read replacement and integration update paths; impact searched all remote-video callers
- [x] Existing pattern / reuse checked: reuse remote mutex and coordinator generation, no timer thread
- [x] Contract understood: timeout polling performs effects after the remote mutex is unlocked
- [x] Risk reviewed: premature Stop, double release, main-loop blocking
- [x] Mitigation recorded: deterministic injected time in tests and idempotent terminal paths

## Validation Checklist
- [x] `make test-protocol-coordinator` exits 0
- [x] `git diff --check` exits 0

## Test Checklist
- [x] Focused AirPlay remote-video binary — all pass

## Implementation Notes
An ordinary active-session close starts a five-second monotonic deadline. Matching control traffic cancels it, while timeout and explicit teardown each clear the matching generation exactly once before invoking player effects.

## Files Changed
`source/protocol/airplay/media/remote_video.h`, `source/protocol/airplay/media/remote_video.c`, `source/protocol/airplay/integration.h`, `source/protocol/airplay/integration.c`, `source/main.c`, `scripts/test_airplay_remote_video.c`.
