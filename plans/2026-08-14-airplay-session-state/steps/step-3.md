# Step 3: Protocol Phase Boundaries

> Status: COMPLETED
> Created: 2026-08-14

## Goal
Replace ambiguous handler flag interpretation with a derived AirPlay protocol phase and explicit transition diagnostics.

## Prerequisites
- Step 2 completed — receiver identity and close semantics are stable.
- Files to modify: `source/protocol/airplay/protocol/handlers.[ch]`, `scripts/test_airplay_handlers.c`, diagnostics documentation.
- Existing pairing and mirror runtime state enums remain authoritative in their domains.

## Deliverables
- A small derived phase distinguishes control, transport ready, record pending, stream ready, and recording states.
- Invalid order is rejected without conflating protocol, bridge, or decoder failures.
- After this step: transcript-like handler tests identify the first failed stage.

## Plan
- [x] `read` handler route branches and tests — list legal request sequences.
- [x] `rg` existing status enums/log stage names — reuse terminology and avoid a generic state-machine utility.
- [x] `edit` handlers — add derived phase helper, transition validation, and low-noise stage logs.
- [x] `edit` handler tests and diagnostics docs — cover legal and out-of-order transcripts.
- [x] `bash` `make test-airplay` — verify protocol sequence behavior.

## Quality Checklist
- [x] Evidence-before-edit: target read handlers/tests, impact search state flags, validation `make test-airplay`
- [x] Existing pattern / reuse checked: preserve existing pairing and mirror runtime states
- [x] Contract understood: phase is derived diagnostics/validation, not a duplicate mutable source of truth
- [x] Risk reviewed: rejecting valid Apple request order, noisy logging
- [x] Mitigation recorded: reference behavior matrix, transcript tests, first-transition-only logging

## Validation Checklist
- [x] `make test-airplay` exits 0
- [x] `git diff --check` exits 0

## Test Checklist
- [x] Valid pair/setup/record sequences pass
- [x] Out-of-order setup/record routes return deterministic protocol errors

## Implementation Notes
- Added a derived AirPlay handler phase from the existing RTSP and stream flags instead of storing a second mutable state.
- Added invariant validation at the handler boundary and deterministic reset after failed setup so partial setup cannot leak into later requests.
- Added transition-only traces for setup, record, teardown, and close; repeated requests do not create per-request phase noise.
- Extended the handler transcript test across control, transport-ready, record-pending, recording, and closed phases.
- A non-trace build initially exposed parameters used only by the trace macro; explicit unused casts keep all build variants warning-clean.

## Files Changed
- `source/protocol/airplay/protocol/handlers.h`
- `source/protocol/airplay/protocol/handlers.c`
- `source/protocol/airplay/receiver.c`
- `scripts/test_airplay_handlers.c`
- `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md`
