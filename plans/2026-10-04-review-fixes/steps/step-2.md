# Step 2: Correct release task and incremental version tracking

> Status: COMPLETED
> Created: 2026-10-04

## Goal
Use formal release builds for packaging and invalidate objects when APP_VERSION changes.
## Prerequisites
- Step 1 complete.
- Findings 3 and 4 approved; no GitHub workflow changes needed.
## Deliverables
- Release task and updated configuration signature, validated with local build.
## Plan
- [x] Add a release-build task and use it in the release composite.
- [x] Include APP_VERSION in the build signature.
- [x] Validate task JSON, dependency tests and incremental build behavior.
## Quality Checklist
- [x] Evidence: tasks, package gate and make targets reviewed.
- [x] Reuse: existing release-build target.
- [x] Contract: keep ordinary development/upload tasks unchanged.
- [x] Risk: stale version or bypassed release checks.
- [x] Mitigation: task graph validation, signature comparison and build.
## Validation Checklist
- [x] Task JSON parses; release composite selects release-build.
- [x] git diff --check passes; deferred coordinator unchanged.
## Test Checklist
- [x] Installer fixtures pass; local build passes.
## Implementation Notes
Added NX-Cast: Build Release using make release-build RELEASE_JOBS=4 and made the existing release composite depend on it before packaging. Existing launch references remain valid; normal rebuild/upload tasks unchanged.

APP_VERSION now participates in the build signature. A temporary build-directory fixture using the real prepare-build-config target confirmed unchanged versions retain a sentinel object and changed versions remove it. The real project version remains 0.3.2.

Validation: 10 FFmpeg installer fixture tests passed; JSON task/dependency references passed; make release-build RELEASE_JOBS=4 completed with no warning/error matches in /tmp/nxcast-approved-fixes-build.log. The installed 7.1-4 package was reused without downloading/installing. Packaging passed and verified sources.txt, NRO layout and AirPlay assets. Output: dist/NX-Cast-sdmc.zip. git diff --check passed. No coordinator/protocol/player or GitHub workflow changes; no upload, commit or release performed. Device testing remains pending.
## Files Changed
- .vscode/tasks.json
- makefile
