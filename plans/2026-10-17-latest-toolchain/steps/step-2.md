# Step 2: Integrate the source toolchain into production workflows

> Status: COMPLETED
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
- [x] Promote candidate Dockerfile, retaining host dependencies required by test-airplay.
- [x] Update reusable resolver and make candidate validation call it.
- [x] Update local Docker entry points and documentation.
- [x] Commit/push scoped changes and verify cloud build and tests.

## Quality Checklist
- [x] Read Dockerfiles, toolchain/build/release/candidate workflows and local entry points.
- [x] Reuse existing candidate source builder and digest resolution.
- [x] Contract: latest changes invalidate cache; app-only changes do not.
- [x] Risk: missing host libraries break existing Build tests.
- [x] Mitigation: run existing tests in the integrated candidate path, no new test suite.

## Validation Checklist
- [x] Shell syntax, YAML structure, and whitespace checks pass.
- [x] Integrated candidate cloud build passes (39382939902, both attempts).

## Test Checklist
- [x] Existing make test-airplay passes inside the new production image.
- [x] Candidate release-build and packaging succeed.
- [x] Shared resolver rerun skips Publish toolchain; both jobs pass.

## Implementation Notes
Commit 3ada50a integrates the shared Dockerfile and reusable resolver. Cloud run
39382939902 passes full build and rerun. Rerun toolchain job took 23 seconds and
application/test job 93 seconds; Publish toolchain was skipped. No main merge,
tag, or release. Existing uncommitted Miracast changes preserved. No local
container runtime was available; real Linux validation ran in Actions instead.

## Files Changed
- Dockerfile (promoted from Dockerfile.candidate, which was removed)
- .github/workflows/toolchain.yml
- .github/workflows/toolchain-candidate.yml
- docker-compose.yml
- scripts/docker_build_release.sh
- docs/ci-toolchain.md
- docs/toolchain-candidate.md
- plans/2026-10-17-latest-toolchain/plan.md
- plans/2026-10-17-latest-toolchain/steps/step-1.md
- plans/2026-10-17-latest-toolchain/steps/step-2.md
