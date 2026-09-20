# Step 3: Update Contracts And Full Validation

> Status: COMPLETED
> Created: 2026-08-17

## Goal
Document the new FCUP status and logical-close semantics, then run full host and Switch validation.

## Prerequisites
- Steps 1-2 completed with no known failures.
- Files to modify: `docs/AIRPLAY_DEVELOPMENT.md`, `docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md`.

## Deliverables
- Development and protocol compatibility documents describe advisory FCUP status and detached active playback after logical connection close.
- Full host, trace, and release validation passes.

## Plan
- [ ] `read` docs/AIRPLAY_DEVELOPMENT.md and docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md — inspect current ownership/FCUP wording.
- [ ] `edit` docs/AIRPLAY_DEVELOPMENT.md — update playback lifecycle wording.
- [ ] `edit` docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md — update `/action` and ownership rules.
- [ ] `bash` git diff --check
- [ ] `bash` make test-airplay
- [ ] `bash` make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1
- [ ] `bash` make release-build RELEASE_JOBS=4

## Quality Checklist
- [ ] Evidence-before-edit: docs and validation commands read; validation discovered from makefile.
- [ ] Existing pattern / reuse checked: retain current tables and experimental status.
- [ ] Contract understood: automated success is not hardware acceptance.
- [ ] Risk reviewed: documentation accuracy / observability.
- [ ] Mitigation recorded: physical retest remains required.

## Validation Checklist
- [ ] `git diff --check` exits 0
- [ ] `make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1` exits 0
- [ ] `make release-build RELEASE_JOBS=4` exits 0

## Test Checklist
- [ ] `make test-airplay` — all pass

## Implementation Notes
- Added the advisory FCUP status and detached logical-close contracts to both AirPlay documents.
- Host tests, strict full-trace build, and release build all pass.
- Rebuilt the final full-trace `NX-Cast.nro` after the release build so the hardware test artifact contains the new diagnostics.

## Files Changed
- `docs/AIRPLAY_DEVELOPMENT.md`
- `docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md`
