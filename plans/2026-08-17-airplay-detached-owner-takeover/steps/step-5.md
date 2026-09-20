# Step 5: Run Full Host And Switch Validation

> Status: COMPLETED
> Created: 2026-08-17

## Goal
Validate the corrected takeover plan with the complete host suite, strict trace build, release build, and a final trace NRO for real-device testing.

## Prerequisites
- Step 4 completed with no known failures.
- devkitPro environment available at `/opt/devkitpro/switchvars.sh`.

## Deliverables
- All validation commands exit 0.
- Final full-trace `NX-Cast.nro` contains takeover diagnostics and a recorded SHA-256.
- Real-device retest checklist is explicit and executable.

## Plan
- [ ] `bash` git diff --check — expect exit 0.
- [ ] `bash` make test-airplay — expect exit 0.
- [ ] `bash` make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1 — expect exit 0.
- [ ] `bash` make release-build RELEASE_JOBS=4 — expect exit 0.
- [ ] `bash` make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1 — rebuild final trace artifact after release.
- [ ] `bash` shasum -a 256 NX-Cast.nro — record the artifact hash.

## Quality Checklist
- [ ] Evidence-before-edit: validation commands sourced from makefile and previous plan.
- [ ] Existing pattern / reuse checked: same strict feature gates as completed regression plan.
- [ ] Contract understood: automated success is not hardware acceptance.
- [ ] Risk reviewed: Switch runtime, protocol ownership, and release package.
- [ ] Mitigation recorded: real-device takeover test remains mandatory after automated validation.

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
- Final trace SHA-256: `57afa88cd826dfc2307323c356a897da8f1a4834b8e2e1855b75172002d3ba27`.

## Files Changed
- No source files changed in this verification-only step.
