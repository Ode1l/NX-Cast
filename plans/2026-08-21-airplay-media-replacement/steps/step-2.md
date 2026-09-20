# Step 2: Integration Cleanup Policy And Regression Validation

> Status: COMPLETED
> Created: 2026-08-21

## Goal
Remove the generic active-media detached timer and verify explicit termination and unrelated playback protocols remain intact.

## Prerequisites
- Step 1 completed with the remote-video lifecycle owning disconnect semantics.
- Files to modify: `source/protocol/airplay/integration.c` and affected tests/docs only if required by direct references.

## Deliverables
- Integration no longer converts an ordinary AirPlay control disconnect into delayed `PLAYER_COMMAND_STOP`.
- Explicit stop, shutdown, owner takeover, pending negotiation failure, and stale lease validation remain operational.
- After this step: AirPlay host suite and full trace Switch build pass.

## Plan
- [x] `edit` `source/protocol/airplay/integration.c` — remove detached timer fields/callback/release path while preserving explicit stop and takeover release.
- [x] `rg` source and scripts for `remote_detach|detached-cleanup|airplay_integration_tick` — no obsolete cleanup policy or dead tick API remains.
- [x] `bash` `make test-airplay` — protocol/coordinator/player regression suite passes.
- [x] `bash` `make full-trace-build -j4` — Switch integration build passes.
- [x] `bash` `git diff --check` — patch hygiene passes.

## Quality Checklist
- [x] Evidence-before-edit: re-read integration target, impact search callbacks/tick callers, validation commands identified.
- [x] Existing pattern / reuse checked: explicit `integration_remote_release` remains the single release mechanism.
- [x] Contract understood: only explicit protocol/application terminal events release active media.
- [x] Risk reviewed: shutdown and owner-takeover regression.
- [x] Mitigation recorded: full AirPlay tests plus Switch build and stale-lease tests.

## Validation Checklist
- [x] `make test-airplay` exits 0.
- [x] `make full-trace-build -j4` exits 0.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] Existing remote-video, coordinator, actor, lifecycle, and shutdown tests pass through `make test-airplay`.

## Implementation Notes
Removed the 1.5-second detached cleanup state and callback registration. `integration_remote_release` now releases only; callers submit Stop explicitly, eliminating duplicate Stop commands. Since the timer was the only tick responsibility, the empty tick declaration, implementation, and main-loop call were removed. Optional `control_detached` remains in the remote-video test contract as an observer but has no production cleanup policy.

## Files Changed
- `source/protocol/airplay/integration.c`
- `source/protocol/airplay/integration.h`
- `source/main.c`
- `plans/2026-08-21-airplay-media-replacement/plan.md`
- `plans/2026-08-21-airplay-media-replacement/steps/step-2.md`
