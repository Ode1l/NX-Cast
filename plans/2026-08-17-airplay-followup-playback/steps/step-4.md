# Step 4: Run Full Host And Switch Validation

> Status: COMPLETED
> Created: 2026-08-17

## Goal
Validate the follow-up fixes with the complete host suite, strict trace build, release build, and final trace artifact.

## Prerequisites
- Step 3 completed.
- devkitPro environment available at `/opt/devkitpro/switchvars.sh`.

## Deliverables
- All validation commands exit 0.
- Final full-trace `NX-Cast.nro` is rebuilt with a recorded SHA-256.
- Real-device retest checklist is explicit.

## Plan
- [ ] `bash` git diff --check — expect exit 0.
- [ ] `bash` make test-airplay — expect exit 0.
- [ ] `bash` make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1 — expect exit 0.
- [ ] `bash` make release-build RELEASE_JOBS=4 — expect exit 0.
- [ ] `bash` make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1 — rebuild final trace artifact after release.
- [ ] `bash` shasum -a 256 NX-Cast.nro — record the artifact hash.

## Quality Checklist
- [ ] Evidence-before-edit: validation commands sourced from makefile and prior plans.
- [ ] Existing pattern / reuse checked: same strict feature gates as completed AirPlay plans.
- [ ] Contract understood: automated success is not hardware acceptance.
- [ ] Risk reviewed: Switch runtime, protocol compatibility, and packaging.
- [ ] Mitigation recorded: hardware retest remains mandatory.

## Validation Checklist
- [ ] `git diff --check` exits 0
- [ ] `make test-airplay` exits 0
- [ ] full trace build exits 0
- [ ] release build exits 0

## Test Checklist
- [ ] `make test-airplay` — all pass

## Implementation Notes
- `git diff --check`, `make test-airplay`, strict full-trace build, and release build all passed.
- Rebuilt the final full-trace artifact after release.
- Final trace SHA-256: `e12bc0d5189f0ee7f0b1059035b12233af74d704cd699a1bcc9ebe06b6a1a614`.

## Files Changed
- No source files changed in this verification-only step.
