# Step 2: Validate And Package Release

> Status: IN_PROGRESS
> Created: 2026-09-19

## Goal
Produce and inspect a strict local v0.3.0 release package before creating the public tag.

## Prerequisites
- Step 1 completed with consistent v0.3.0 metadata.
- Supported FFmpeg/libmpv/deko3d and host test dependencies remain installed.

## Deliverables
- Passing AirPlay and shared host test suite.
- Release-attested `NX-Cast.nro` built without trace flags.
- Verified `dist/NX-Cast-sdmc.zip` containing intact presets, font, licenses, and one NRO.
- After this step: the exact release source is locally validated and packageable.

## Plan
- [ ] `bash` `make test-airplay` — run the release workflow host gate.
- [ ] `bash` `make RELEASE_JOBS=4 release-build` — create strict release NRO.
- [ ] `bash` package script with minimum-size gate — produce release assets.
- [ ] `bash` inspect attestation, ZIP listing, version strings, hashes, and sensitive-file exclusions.

## Quality Checklist
- [ ] Evidence-before-edit: workflow and package script read; no source edits planned.
- [ ] Existing pattern / reuse checked: exact CI commands reused locally.
- [ ] Contract understood: package must contain one directory-local NRO and complete runtime assets.
- [ ] Risk reviewed: trace binary, stale assets, missing font/presets, private AirPlay data.
- [ ] Mitigation recorded: release attestation plus package script and explicit ZIP inspection.

## Validation Checklist
- [ ] Strict release build exits 0.
- [ ] Package script exits 0 and both assets are non-empty.

## Test Checklist
- [ ] `make test-airplay` passes all host tests.
- [ ] IPTV channel-list navigation test passes through the suite/workflow-equivalent gate.

## Implementation Notes
Pending.

## Files Changed
Pending.
