# Step 3: Physical Test Playbook

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Document an ordered, repeatable physical test matrix and validate every code/test contract available on the current computer.

## Prerequisites
- Step 2 completed with profile markers and subsystem liveness evidence.
- Files to modify: `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md`, `docs/README.md`, and plan records.

## Deliverables
- Profile selection, fixed test timing, expected observations, result table, and decision tree; toolchain setup remains user-owned.
- Explicit handling of nxlink shutdown output and incomplete logs.
- After this step: the user can execute multiple rounds and return unambiguous evidence for analysis.

## Plan
- [x] `write` `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md` - documented profile flags, fixed interaction script, result table, and interpretation tree without prescribing Windows tooling.
- [x] `edit` `docs/README.md` - linked the diagnostic playbook.
- [x] `bash` host suites, strict Switch trace build, and `git diff --check` - recorded zero failures.
- [x] `edit` plan and step files - completed implementation notes, files changed, and validation results.

## Quality Checklist
- [x] Evidence-before-edit: commands and profile behavior verified from implemented Makefile/source contracts.
- [x] Existing pattern / reuse checked: AirPlay development and threading docs remain authoritative for architecture; this document covers only diagnosis.
- [x] Contract understood: identical test actions and durations across one-variable builds.
- [x] Risk reviewed: tester ambiguity, old logs/NROs, different computer paths, and false inference from connection reset.
- [x] Mitigation recorded: hashes, profile banners, timestamped labelled logs, and explicit result fields.

## Validation Checklist
- [x] All host suites and config syntax checks pass.
- [x] Strict diagnostic Switch build exits 0.
- [x] `git diff --check` reports no errors.

## Test Checklist
- [x] Every documented profile passes Makefile dry-run; an unknown profile fails with the valid-name list.
- [x] Every decision-tree branch maps to a subsequent single-variable profile.

## Implementation Notes
The playbook separates a 40-second Home/input liveness gate from the IPTV and
DLNA media gate. It explicitly treats an nxlink connection reset as intentional
shutdown evidence, not the freeze cause. No Windows shell, VS Code, devkitPro,
or upload workflow was added because the user owns that toolchain.

Validation completed with the full AirPlay host suite, three isolated mDNS
start/stop smoke binaries, all profile dry-runs, unknown-profile rejection,
`git diff --check`, and a strict `full-serial` Switch build. The resulting NRO
contains the expected profile label and extended runtime heartbeat string.

## Files Changed
- `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md`
- `docs/README.md`
- `plans/2026-07-22-airplay-freeze-diagnostic-matrix/plan.md`
- `plans/2026-07-22-airplay-freeze-diagnostic-matrix/steps/step-2.md`
- `plans/2026-07-22-airplay-freeze-diagnostic-matrix/steps/step-3.md`
