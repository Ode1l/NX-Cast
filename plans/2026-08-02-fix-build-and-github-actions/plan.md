# Plan: Fix Build And GitHub Actions

> Status: COMPLETED
> Created: 2026-08-02
> Last Updated: 2026-08-02

## Goal
Restore a reproducible local Switch build and align GitHub Actions with the current Makefile and dependency contract.

## Assumptions
- The `airplay` branch and its configured GitHub workflows are the intended build target.
- GitHub CLI authentication permits read access to recent Actions runs and logs.
- Product behavior is out of scope unless a source-level compile error requires a focused correction.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] The repository's documented local build command completes, or any machine-only prerequisite failure is isolated with evidence.
- [x] The latest GitHub Actions compile failure is traced to a specific command or dependency mismatch.
- [x] Workflow and Makefile changes use one explicit build contract and pass available validation.

### Non-goals
- Publishing a release or changing media protocol behavior.

### Edge Cases
- GitHub-hosted runners may not have the same devkitPro packages, generated assets, or writable installation paths as the local machine.

## Design Decisions
None — no design-sensitive changes.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Reproduce and identify the local and GitHub Actions failures. |
| Step 2 | `steps/step-2.md` | COMPLETED | Apply the smallest Makefile/workflow contract correction. |
| Step 3 | `steps/step-3.md` | COMPLETED | Run focused tests, local build validation, and workflow checks. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Focused host tests | `make test-focused` or repository-equivalent discovered in Step 1 | Makefile | yes |
| Local Switch build | repository build target discovered in Step 1 | Makefile/docs | yes |
| Workflow syntax/content | parse YAML and inspect all workflow build invocations | `.github/workflows` | yes |
| Actions failure | `gh run view <run-id> --log-failed` | GitHub Actions | yes |

## Context & Learnings
### Key Decisions
- Treat the Makefile as the build implementation and workflows as callers; workflow duplication should be reduced where practical.
### Gotchas & Warnings
- A Makefile change only requires a workflow edit when it changes a caller-visible target, variable, artifact path, prerequisite, or runner dependency.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `makefile` | Defines local and CI build targets and diagnostic profiles. | Read release/test targets and strict host C11 flags in Step 1. |
| `.github/workflows/` | Defines hosted runner setup, build commands, and artifacts. | Both workflows call `make test-airplay` followed by `make release-build`; no stale target was found. |
| GitHub Actions runs | Provides the authoritative remote failure logs. | Run `30738651733`, job `91471951323`, fails in `test-player-types` at `source/player/types.c:10`. |
| `source/player/types.c` | Implements owned copies used by player media, events, and snapshots. | `strdup()` is the only failing symbol in the latest Actions log. |
### Verified Facts
- The user reports that current GitHub builds fail and asks whether Makefile changes always require workflow changes — confirmed in chat, 2026-08-02.
- GitHub Actions run `30738651733` passes image construction and earlier host tests, then fails before Switch compilation because strict Linux C11 does not declare POSIX `strdup()` — verified by `gh run view --log-failed`, Step 1.
- `make test-player-types` passes on macOS because Darwin exposes `strdup()` under the current headers, demonstrating a host portability difference — verified locally, Step 1.
- The workflow target names, required release variables, and artifact paths currently match the Makefile and packaging script; no Actions edit is needed for this root cause — verified by file inspection and `rg`, Step 1.
- Replacing `strdup()` in player owned-value copying with ISO C allocation/copy fixes the Linux CI compiler error without changing ownership semantics — verified by `make test-player-types` and full host suite, Step 2.
- Two strict-feature-macro differences were also corrected in macOS tests: loopback address spelling and use of a redundant `MSG_DONTWAIT` send flag on an already nonblocking socket — verified by `make test-airplay`, Step 2.
- Strict native `make RELEASE_JOBS=4 release-build` completed and generated a 25,535,162-byte attested NRO — verified locally, Step 3.
- Release packaging preserved `assets/iptv/sources.txt` and required AirPlay/license files; both workflow YAML files parse successfully — verified locally, Step 3.
- Hosted confirmation requires pushing these source changes to trigger a new Actions run; no local evidence suggests another workflow-contract failure.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-02 | Step 1 | Identified a strict C11 portability failure in `player_strdup_or_null`; Docker image construction and workflow invocation are healthy. |
| 2026-08-02 | Step 2 | Applied portable C/test corrections; the complete `make test-airplay` suite passes on macOS. |
| 2026-08-02 | Step 3 | Passed full host tests, strict Switch release build, package verification, workflow YAML parsing, and diff checks. |
