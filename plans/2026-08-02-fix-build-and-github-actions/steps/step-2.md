# Step 2: Align Build Contract

> Status: COMPLETED
> Created: 2026-08-02

## Goal
Apply the smallest correction that makes local and hosted builds use the same supported build contract.

## Prerequisites
- Step 1 completed with an exact root cause and affected caller inventory.
- Files to modify: only the Makefile, workflow, script, or documentation paths proven necessary by Step 1.
- Design: keep implementation in Makefile/scripts and keep workflows as thin callers where possible.

## Deliverables
- Corrected build configuration with no unnecessary product behavior changes.
- After this step: the previously failing command reaches successful compilation or the next external blocker is explicit.

## Plan
- [x] `edit` diagnosed build configuration paths — correct the proven target, variable, dependency, or artifact mismatch.
- [x] `rg` repository build callers — update only callers affected by the changed public build contract.
- [x] `edit` nearby build documentation when the supported command or prerequisite changes.
- [x] `bash` focused build command — verify the original failure no longer occurs.

## Quality Checklist
- [x] Evidence-before-edit: exact target and failure recorded in Step 1.
- [x] Existing pattern / reuse checked: existing helper target/script reused where available.
- [x] Contract understood: local and hosted callers use the same target semantics.
- [x] Risk reviewed: release artifact naming, diagnostic flags, dependencies, and cache behavior.
- [x] Mitigation recorded: surgical edits and focused reproduction.

## Validation Checklist
- [x] The previously failing build command exits successfully or reaches a documented external-only blocker.
- [x] Workflow artifact paths match actual build output.

## Test Checklist
- [x] Focused host tests and build-specific checks pass.

## Implementation Notes
- Replaced internal `strdup()` with `strlen`/`malloc`/`memcpy`, retaining nullable and allocation-failure behavior.
- Corrected two macOS-only host-test portability failures encountered while expanding validation; neither changes production behavior.
- No Makefile, workflow, dependency, target, or artifact contract changed, so documentation did not require an update.

## Files Changed
- `source/player/types.c`
- `scripts/test_airplay_server_lifecycle.c`
- `scripts/test_log_mirror.c`
- `plans/2026-08-02-fix-build-and-github-actions/plan.md`
- `plans/2026-08-02-fix-build-and-github-actions/steps/step-2.md`
