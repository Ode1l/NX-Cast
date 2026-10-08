# Step 1: Inspect and validate code risks

> Status: COMPLETED
> Created: 2026-10-04

## Goal
Produce evidence-backed findings instead of speculative explanations for historical failures.

## Prerequisites
- User requested a code review; no business changes authorized in this step.
- Current code and available focused test inventory inspected.

## Deliverables
- Prioritized findings with exact source references and any validation limitations.

## Plan
- [x] Review Make/bootstrap/package/version invalidation behavior and callers.
- [x] Follow high-risk runtime command and resource lifecycles.
- [x] Validate candidate findings and discard unsupported hypotheses.
- [x] Record findings for reporting without changing application code.

## Quality Checklist
- [x] Evidence-before-edit: review only; no production edits.
- [x] Reuse: existing test fixtures and state contracts before creating reproductions.
- [x] Contract: distinguish confirmed code defects from unverified device symptoms.
- [x] Risk: misleading regression claims or accidentally mutating global dependencies.
- [x] Mitigation: isolated fixtures, read-only commands and precise scope.

## Validation Checklist
- [x] Each reported finding has a reachable trigger and source location.
- [x] Worktree contains only review records, no production changes.

## Test Checklist
- [x] Relevant minimal checks recorded; hardware-only gaps identified.

## Implementation Notes
Review completed; this is not a declaration that the application or all tests are defect-free.

Confirmed findings:
1. IPTV writers remove the original before renaming the replacement, then delete the replacement on failure (iptv.c:606, 642, 727). A rename-failure injection into the real favorites writer left neither file recoverable. The existing home language saver demonstrates a backup/rollback approach without requiring a new storage framework.
2. Coordinator tick publishes an AirPlay status fetched outside the lock without revalidating its service lifecycle (protocol_coordinator.c:1152-1163). A deterministic host scheduling harness returned an old running read after the resource worker completed stop; the state became RUNNING while the service was actually stopped and HOME convergence skipped restart. This concerns the opt-in exclusive resource path, not default release KEEP_RECEIVERS=1.
3. VS Code Rebuild + Package Release invokes Clean then dev-build, while package_release.sh requires the attestation only emitted by release-build (tasks.json:183-187). The present dev output was rejected with the expected missing-attestation error.
4. APP_VERSION is a compiler definition but absent from NXCAST_BUILD_CONFIG_KEY (makefile:106, 135-136). An APP_VERSION-only incremental dry run emitted no compilation. A clean release build is unaffected.

Validation:
- python3 scripts/test_ffmpeg_install.py: 10 passed, fixture-only package manager.
- make test-player-actor: passed.
- make test-protocol-coordinator: initial failure at test_exclusive_resource_restart_retries, line 1291; rerun of the unchanged binary passed. Do not report this suite as reliably passing.
- make test-iptv-data: passed with 10041 channels, 81 sources, 100 groups and 10001 favorites.
- /tmp/nxcast-review-coordinator-race: defect asserted successfully twice.
- /tmp/nxcast-review-iptv-save: saved=0 original_exists=0 temporary_exists=0.
- make -n dev-build NXCAST_AUTO_INSTALL_FFMPEG=0 APP_VERSION=0.3.99: Nothing to be done for all; no build/install executed.
- package_release.sh with missing attestation: expected exit 1 before dist mutations.
- git diff --stat: empty; git status only lists this review directory.

Limitations: no Switch testing, no claim that these issues caused previous playback failures. No application fixes, additional CI gates or dependency changes were made. Actor self-destruction and queue saturation theories were not reported without a demonstrated production call path.

## Files Changed
- plans/2026-10-04-code-review/plan.md
- plans/2026-10-04-code-review/steps/step-1.md
- /tmp/nxcast-review-coordinator-race.c (temporary, outside repository)
- /tmp/nxcast-review-iptv-save.c (temporary, outside repository)
