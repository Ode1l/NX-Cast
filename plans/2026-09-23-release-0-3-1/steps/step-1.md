# Step 1: Build and publish v0.3.1

> Status: IN_PROGRESS
> Created: 2026-09-23

## Goal
Create and publish a validated v0.3.1 release whose UI and package metadata show the correct version.

## Prerequisites
- User requested release v0.3.1.
- Files to modify: `makefile`, `source/protocol/dlna/server_info.c`, `CHANGELOG.md`, `.github/release-notes.md`, `README.md`.
- Current UI/IPTV worktree is intended release content.

## Deliverables
- Version metadata and release docs consistently identify v0.3.1.
- Validated complete SD-card package and published GitHub Release triggered by tag `v0.3.1`.

## Plan
- [ ] Update the single APP_VERSION source, DLNA fallback, changelog, release notes, and release instructions.
- [ ] Run diff hygiene, host tests, release build, and SD package validation.
- [ ] Review the full staged diff, commit the intended worktree, push the branch and annotated v0.3.1 tag.
- [ ] Verify the GitHub release and attached `NX-Cast-sdmc.zip`.

## Quality Checklist
- [ ] Evidence-before-edit: version targets and current release docs inspected; version-use search completed; validation commands identified.
- [ ] Existing pattern / reuse checked: APP_VERSION compile define reused for NACP/UI/DLNA.
- [ ] Contract understood: the version must be consistent in application UI and package metadata.
- [ ] Risk reviewed: release/tag publication and packaging contents.
- [ ] Mitigation recorded: validate build and archive before pushing release tag.

## Validation Checklist
- [ ] `git diff --check`
- [ ] `make test-ui`
- [ ] `make test-iptv-data`
- [ ] `make RELEASE_JOBS=4 release-build`
- [ ] `sh scripts/package_release.sh`

## Test Checklist
- [ ] UI and IPTV data host tests pass; release artifact exists and package checks pass.

## Implementation Notes
Pending.

## Files Changed
Pending.
