# Step 3: Verify Build And Workflow

> Status: COMPLETED
> Created: 2026-08-02

## Goal
Verify the corrected build locally and validate the GitHub Actions workflow contract end to end as far as the current environment permits.

## Prerequisites
- Step 2 completed with the diagnosed build correction applied.
- Files to modify: validation bookkeeping only unless verification exposes a directly related defect.
- Design: no additional scope beyond failures introduced or uncovered by the correction.

## Deliverables
- Local test/build evidence and validated workflow configuration.
- After this step: the user has a precise result and knows whether a push is required for hosted confirmation.

## Plan
- [x] `bash` focused host tests — verify build helpers and lifecycle tests remain green.
- [x] `bash` clean local Switch build — verify compilation and artifact generation.
- [x] `bash` YAML parse and workflow invocation checks — validate syntax and target/artifact consistency.
- [x] `bash` `git diff --check` and scoped diff review — catch formatting or unintended changes.
- [x] `read` final changed files — summarize the build contract and remaining hosted-only risks.

## Quality Checklist
- [x] Evidence-before-edit: N/A — verification-first step.
- [x] Existing pattern / reuse checked: repository validation commands reused.
- [x] Contract understood: successful local artifact plus syntactically valid thin workflow caller.
- [x] Risk reviewed: GitHub-hosted run cannot be proven without pushing the change.
- [x] Mitigation recorded: distinguish local proof from hosted confirmation.

## Validation Checklist
- [x] Local Switch build result recorded.
- [x] Workflow YAML and build/artifact references validated.
- [x] `git diff --check` passes.

## Test Checklist
- [x] Relevant focused tests pass with zero failures.

## Implementation Notes
- `make test-airplay` passed all compile, unit, lifecycle, pairing, mirror, audio, and smoke checks.
- `make RELEASE_JOBS=4 release-build` completed with release attestation and produced a 25,535,162-byte NRO.
- `scripts/package_release.sh` produced the standalone NRO and SD package and verified IPTV presets, AirPlay storage documentation, licenses, and sensitive-file exclusions.
- Ruby YAML parsing and `git diff --check` passed. A new hosted run remains push-triggered.

## Files Changed
- `plans/2026-08-02-fix-build-and-github-actions/plan.md`
- `plans/2026-08-02-fix-build-and-github-actions/steps/step-3.md`
