# Step 2: Validate Regression Recovery

> Status: COMPLETED

## Goal
Verify host and Switch behavior after the bounded HLS size fix.

## Prerequisites
- Step 1 completed.

## Deliverables
- Full validation evidence and residual-risk report.

## Plan
1. Run all AirPlay host tests.
2. Build the Switch target.
3. Run patch hygiene checks and update the plan.

## Quality Checklist
- [x] No unrelated dirty-worktree content is reverted.

## Validation Checklist
- [x] Full AirPlay tests pass.
- [x] Switch build passes.

## Test Checklist
- [x] Focused and aggregate tests pass together.

## Implementation Notes
- `make test-airplay` passed, including remote HLS response encoding and composed receiver smoke coverage.
- `make dev-build BUILD_JOBS=4` produced `NX-Cast.nro`.
- `git diff --check` passed.
- Real-device confirmation remains required. For YouTube, verify that condensed rewrite reaches `hls-ready` without `rewrite-failed`. Bilibili's observed sender-side control/reverse close remains a separate unknown and was not changed speculatively.

## Files Changed
- `plans/2026-09-02-airplay-hls-size-regression/plan.md`
- `plans/2026-09-02-airplay-hls-size-regression/steps/step-2.md`
