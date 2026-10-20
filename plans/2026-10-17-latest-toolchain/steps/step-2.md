# Step 2: Integrate the source toolchain into production workflows

> Status: IN_PROGRESS
> Created: 2026-10-20

## Goal
Use a single latest-base source image recipe and reusable resolver for candidate, Build, Release, and local container builds.

## Prerequisites
- Step 1 cloud build/cache verification passed; user confirmed cloud playback and approved integration.
- Preserve unrelated dirty Miracast files; no new release or automatic main merge.

## Deliverables
- Shared Dockerfile and resolver with base-digest plus recipe hashing.
- Candidate workflow validates the actual production path without publishing a release.
- Updated local entry points and documentation.

## Plan
- [ ] Promote candidate Dockerfile, retaining host dependencies required by test-airplay.
- [ ] Update reusable resolver and make candidate validation call it.
- [ ] Update local Docker entry points and documentation.
- [ ] Commit/push scoped changes and verify cloud build and tests.

## Quality Checklist
- [x] Read Dockerfiles, toolchain/build/release/candidate workflows and local entry points.
- [x] Reuse existing candidate source builder and digest resolution.
- [x] Contract: latest changes invalidate cache; app-only changes do not.
- [x] Risk: missing host libraries break existing Build tests.
- [x] Mitigation: run existing tests in the integrated candidate path, no new test suite.

## Validation Checklist
- [ ] Shell syntax, YAML structure, and whitespace checks pass.
- [ ] Integrated candidate cloud build passes.

## Test Checklist
- [ ] Existing make test-airplay passes inside the new production image.
- [ ] Candidate release-build and packaging succeed.

## Implementation Notes
Pending implementation. User authorization is for toolchain integration; no tag/release creation or merge of unrelated Miracast history.

## Files Changed
- Pending.
