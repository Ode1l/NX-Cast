# Step 1: Diagnose Local And Hosted Builds

> Status: COMPLETED
> Created: 2026-08-02

## Goal
Produce an evidence-backed root cause for the current local and GitHub Actions build failures.

## Prerequisites
- Files to modify: workflow bookkeeping only during diagnosis.
- GitHub CLI access is available, or its absence is recorded as a validation limitation.
- Design: no build configuration changes until the failing command and contract mismatch are identified.

## Deliverables
- An inventory of Makefile targets, workflow callers, dependencies, and artifact paths.
- After this step: the exact failing local and hosted commands are known and Step 2 has a bounded edit scope.

## Plan
- [x] `read` `makefile`, `.github/workflows/*.yml`, and build documentation — map targets, variables, dependencies, and artifacts.
- [x] `bash` `git log`/`git diff` for Makefile and workflows — identify recent contract drift.
- [x] `bash` `gh auth status`, `gh run list`, and `gh run view --log-failed` — capture the latest hosted failure.
- [x] `bash` repository local build and focused test commands — reproduce or isolate the failure.
- [x] `rg` build target/variable references — ensure all callers of any failing contract are accounted for.

## Quality Checklist
- [x] Evidence-before-edit: target read `makefile`/`.github/workflows`, impact search `rg`, validation local build and Actions logs.
- [x] Existing pattern / reuse checked: existing CI helper targets and scripts will be preferred over duplicated workflow commands.
- [x] Contract understood: workflows call Makefile targets with runner-installed dependencies and collect declared artifacts.
- [x] Risk reviewed: runner environment drift, package availability, generated assets, and local-only assumptions.
- [x] Mitigation recorded: use exact failed command and compare local/hosted environment before editing.

## Validation Checklist
- [x] Latest failed Actions run and failing job are identified.
- [x] Local build result is recorded with the exact command and first actionable error.

## Test Checklist
- [x] Existing focused host tests are run if their dependencies are available.

## Implementation Notes
- Latest run `30738651733` failed in `make test-airplay` while compiling `test-player-types` with strict Linux C11.
- `source/player/types.c:10` calls POSIX `strdup()` without enabling a POSIX feature-test macro; GCC reports an implicit declaration and `int`-to-pointer conversion under `-Werror`.
- macOS passes the same command, so the required correction is portable C source rather than a workflow change.

## Files Changed
- `plans/2026-08-02-fix-build-and-github-actions/plan.md`
- `plans/2026-08-02-fix-build-and-github-actions/steps/step-1.md`
