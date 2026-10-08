# Plan: Review current build and runtime risks

> Status: COMPLETED
> Created: 2026-10-04
> Last Updated: 2026-10-04

## Goal
Report concrete current-code defects with severity, trigger, impact and source references, without changing application behavior.

## Assumptions
- User requests assessment, not immediate fixes.
- A focused review can identify obvious defects but cannot certify all device/runtime behavior.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] Review recent automatic dependency preparation and the existing build/packaging callers.
- [x] Inspect playback ownership, asynchronous operations and shutdown/resource handling for demonstrable hazards.
- [x] Validate findings with reachable code paths or minimal isolated reproductions.
- [x] Prepare prioritized findings, limitations and recommended repair order.
### Non-goals
- Business-code edits, package installs, new releases or broad refactoring.
### Edge Cases
- Distinguish source-confirmed defects from theories about previous hardware failures.

## Design Decisions
None - read-only review except workflow records.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | steps/step-1.md | COMPLETED | Inspect high-risk paths and validate prioritized findings |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Source evidence | rg; nl -ba; targeted file reads | Current main | yes |
| Build behavior | Isolated temporary fixtures/dry runs as needed | Existing installer tests and Make rules | as needed |
| Runtime contracts | Existing focused host tests or minimal reproductions | scripts/test_*.c | as needed |
| Review integrity | git diff --stat; git status --short | Git | yes |

## Context & Learnings
### Key Decisions
- Review recent build automation first, then bounded high-risk runtime areas rather than attempting an unbounded repository audit.
### Gotchas & Warnings
- Passing tests or successful device playback does not disprove reachable lifecycle errors.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| makefile, scripts/install_switch_ffmpeg_airplay.sh | Newly added build preparation | Current main and latest commit read |
| .vscode/tasks.json, scripts/package_release.sh | Build consumers and release gate | Task graph and attestation failure verified |
| source/app, source/player/core | Playback ownership and command serialization | Coordinator stale-status race reproduced in isolated host harness |
| source/protocol/airplay, source/iptv, source/log | Worker/resource boundaries | Targeted lifecycle reads; IPTV save failure reproduced |
| /tmp/nxcast-review-coordinator-race.c | Temporary deterministic scheduling harness | Includes existing coordinator test fixtures, no production changes |
| /tmp/nxcast-review-iptv-save.c | Temporary rename-failure harness | Includes existing IPTV test fixtures, temporary data only |
### Verified Facts
- Current main is 32b8da1, worktree was clean before review; the latest build automation is 6dd7127.
- FFmpeg installer fixtures: 10 tests passed; no global SDK changes or downloads performed.
- Media actor and IPTV data tests passed. Coordinator suite failed once at test_protocol_coordinator.c:1291, then passed unchanged; it is not consistently green.
- Deterministic coordinator harness passed twice by asserting the defect: a pre-stop running status overwrites STOPPED and returning home does not restart AirPlay. This path requires exclusive resource management; main disables it with the default KEEP_RECEIVERS=1.
- IPTV rename-failure harness confirmed both the original favorites file and its temporary replacement are absent after a failed save. Source-list writers use the same delete-before-rename pattern.
- VS Code release composite calls clean/dev-build, not release-build; packaging rejects the missing release attestation. The existing dev-build output reproduced that rejection without modifying dist.
- A dry run of dev-build with APP_VERSION=0.3.99 reports nothing to do. APP_VERSION is absent from the configuration signature, so incremental objects can retain old UI/DLNA version strings. release-build cleans and is not affected by this incremental issue.
- No production edits. Hardware/network playback was not retested. These findings do not establish the cause of prior device symptoms.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-04 | Step 1 | Completed review with four evidence-backed findings; existing defects left unchanged as requested. |
