# Step 3: Validate and Package Profile 13

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Run the integrated host regressions and produce a clean Profile 13 NRO for physical AirPlay/DLNA verification.

## Prerequisites
- Steps 1 and 2 completed with their focused tests passing.
- The installed devkitPro, libnx, switch-libmpv_deko3d, FFmpeg, and AirPlay crypto dependencies remain available in the MSYS environment.
- No unrelated dirty-worktree changes are staged, reverted, or reformatted.

## Deliverables
- Passing runnable DLNA session, coordinator, and shutdown checks plus successful AirPlay target compilation/linking.
- A successful clean `full-owner-exclusive-bsd12` Switch build with recorded artifact hash.
- After this step: the user can test AirPlay video arrival and DLNA mobile-exit home restoration on hardware.

## Plan
- [x] `bash` runnable integrated host regressions — focused DLNA controller, protocol coordinator, and shutdown-order checks pass; full AirPlay host linking remains blocked by pre-existing missing host libraries.
- [x] `bash` the clean Profile 13 Switch build command from `plan.md` — compile/link the test NRO with required dependencies enforced.
- [x] `bash` `Get-FileHash NX-Cast.nro -Algorithm SHA256` — record the exact physical-test artifact hash.
- [x] `read` `git diff --check` and task-path symbol/diff review — verify formatting and that task edits are accounted for amid the pre-existing dirty worktree.
- [x] `edit` the plan and step files — record commands, results, changed files, and final status.

## Quality Checklist
> Evidence summary only. Detailed guidance lives in `references/code-quality.md`, `references/risk-classification.md`, and `references/verify-step.md`.

- [x] Evidence-before-edit: target read task diffs, impact search task symbols, validation all available commands in `plan.md`
- [x] Existing pattern / reuse checked: existing MSYS/devkitPro build workflow and Profile 13 are reused
- [x] Contract understood: host tests protect pure state; Switch build protects platform linkage; physical test remains required for real iPhone/controller behavior
- [x] Risk reviewed: correctness / performance / observability / project-fit and host/platform divergence
- [x] Mitigation recorded: clean required-dependency build, artifact hash, scoped diff review, explicit physical test handoff

## Validation Checklist
- [x] Every runnable host regression exits 0; unavailable AirPlay host dependencies are explicitly recorded rather than treated as a code failure.
- [x] Clean Profile 13 Switch build exits 0 and emits `NX-Cast.nro`.
- [x] `git diff --check` reports no whitespace errors.

## Test Checklist
- [x] DLNA controller/coordinator/shutdown tests pass; AirPlay regression sources compile in the Switch target and await dependency-capable host or physical execution.
- [x] Hardware checklist prepared: AirPlay audio+video, AirPlay end, DLNA normal Stop, DLNA app exit without Stop, IPTV regression, B fallback.

## Implementation Notes
Ran a clean Profile 13 build with explicit `DEVKITA64`, target PATH, and `PORTLIBS_PREFIX` because this devkitPro installation has no `switchvars.sh` and current switch rules do not populate the project-specific `PORTLIBS_PREFIX` name. The build compiled every source and linked `NX-Cast.nro`. Re-ran the pure controller-session test, coordinator test with the existing Cygwin `_GNU_SOURCE` requirement, and shutdown-order test with the bundled Python runtime. Full AirPlay host execution remains unavailable because x86_64 mbedTLS/libsodium/FFmpeg development packages are not installed; the new test cases are retained and all affected production files compile/link for aarch64. Final SHA-256: `03A692C937E4299F5EFAE10B8F7E9069522A2E6760D0DEA5E0DE844F630A20C2`.

## Files Changed
- `NX-Cast.nro` (generated test artifact)
- `plans/2026-07-22-airplay-audio-first-dlna-exit/plan.md`
- `plans/2026-07-22-airplay-audio-first-dlna-exit/steps/step-3.md`
