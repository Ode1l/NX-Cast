# Step 3: Documentation and End-to-End Validation

> Status: COMPLETED
> Created: 2026-08-16

## Goal
Document the global installation model and validate the local and CI-equivalent build contracts.

## Prerequisites
- Step 1 local entry points completed.
- Step 2 CI wiring completed.

## Deliverables
- README and toolchain documentation explain the one-time global install and CI behavior.
- Static tests and available host tests pass.
- After this step: developers can reproduce the same FFmpeg capabilities used by GitHub build and release.

## Plan
- [x] `edit README.md` — replace manual package steps with the Make target and explain compile-time/global semantics.
- [x] `edit docs/ffmpeg-mpv-toolchain.md` — document local privilege boundary, verifier, and CI installation path.
- [x] `bash bash -n ...` and `bash make test-airplay` — validate scripts and host behavior.
- [x] `bash git diff --check` — reject whitespace errors.

## Quality Checklist
- [x] Evidence-before-edit: existing README/toolchain sections read; validation commands derived from Make and workflow files.
- [x] Existing pattern / reuse checked: updated current toolchain guide rather than creating a parallel guide.
- [x] Contract understood: Matroska is linked into target FFmpeg and does not ship as an SD-card runtime file.
- [x] Risk reviewed: misleading users into installing host FFmpeg or expecting runtime plugin discovery.
- [x] Mitigation recorded: exact target prefix and verification commands documented.

## Validation Checklist
- [x] `bash -n scripts/build_switch_ffmpeg_airplay.sh scripts/install_switch_ffmpeg_airplay.sh scripts/verify_switch_ffmpeg_airplay.sh` exits 0.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] `make test-airplay` passes.
- [x] `make verify-airplay-ffmpeg` behavior matches whether the global package is installed.

## Implementation Notes
`make test-airplay` passed. A real pinned package rebuild completed and the extracted target prefix passed the shared verifier; a generated AArch64 fixture also exercised the verifier success path. The current global prefix correctly exercises the missing-muxer failure path until the user performs the one-time sudo install. Docker, `actionlint`, and PyYAML are unavailable locally, so Actions execution remains the final remote validation.

## Files Changed
- `README.md`
- `docs/ffmpeg-mpv-toolchain.md`
- `plans/2026-08-16-global-matroska-toolchain/plan.md`
- `plans/2026-08-16-global-matroska-toolchain/steps/step-3.md`
