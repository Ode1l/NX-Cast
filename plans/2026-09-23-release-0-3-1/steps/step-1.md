# Step 1: Build and publish v0.3.1

> Status: COMPLETED
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
- [x] Update the single APP_VERSION source, DLNA fallback, changelog, release notes, and release instructions.
- [x] Run diff hygiene, host tests, release build, and SD package validation.
- [x] Review the full staged diff, commit the intended worktree, push the branch and annotated v0.3.1 tag.
- [x] Verify the GitHub release and attached `NX-Cast-sdmc.zip`.

## Quality Checklist
- [x] Evidence-before-edit: version targets and current release docs inspected; version-use search completed; validation commands identified.
- [x] Existing pattern / reuse checked: APP_VERSION compile define reused for NACP/UI/DLNA.
- [x] Contract understood: the version must be consistent in application UI and package metadata.
- [x] Risk reviewed: release/tag publication and packaging contents.
- [x] Mitigation recorded: validate build and archive before pushing release tag.

## Validation Checklist
- [x] `git diff --check`
- [x] `make test-ui`
- [x] `make test-iptv-data`
- [x] `make RELEASE_JOBS=4 release-build`
- [x] `sh scripts/package_release.sh`

## Test Checklist
- [x] UI and IPTV data host tests pass; release artifact exists and package checks pass.

## Implementation Notes
Host tests passed; local and GitHub release builds/package checks passed. The NRO and NACP both contain version `0.3.1`. GitHub release: https://github.com/Ode1l/NX-Cast/releases/tag/v0.3.1. Asset: `NX-Cast-sdmc.zip` (19,843,168 bytes; SHA-256 `a16763ec60081513b5b7648abd782b004d99b0c270955ca2b7a7d879550454c4`).

## Files Changed
- `makefile`
- `source/protocol/dlna/server_info.c`
- `CHANGELOG.md`
- `.github/release-notes.md`
- `README.md`
- Release worktree changes staged as commit `18c8a7e`.
