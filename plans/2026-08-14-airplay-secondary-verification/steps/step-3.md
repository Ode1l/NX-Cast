# Step 3: Validate the Switch AirPlay Trace Build

> Status: COMPLETED
> Created: 2026-08-14

## Goal
Produce a strict trace NRO for one focused real-device rerun while preserving DLNA/IPTV behavior.

## Prerequisites
- Steps 1 and 2 completed with all host tests passing.
- AirPlay-capable staged Switch portlibs remain available.
- Files to modify: plan records only unless validation exposes a scoped regression.

## Deliverables
- Complete AirPlay host suite, mDNS smoke, strict Switch build, and diff hygiene pass.
- Generated NRO hash and next-run markers are recorded.
- After this step: one upload can verify whether secondary pairing proceeds to `/play` or type-110 mirroring.

## Plan
- [x] `bash` `make test-airplay && python3 scripts/smoke_airplay_mdns.py` — run complete host regressions.
- [x] `bash` strict Switch trace build with staged portlibs — require libmpv, deko3d, Ed25519, and AirPlay muxer.
- [x] `bash` `git diff --check` and hash `NX-Cast.nro` — verify artifact and patch hygiene.

## Quality Checklist
- [x] Evidence-before-edit: N/A — verification-only; no task source changes in this step.
- [x] Existing pattern / reuse checked: previous strict trace build command and staged dependency root.
- [x] Contract understood: build success is not real-device media success.
- [x] Risk reviewed: stale NRO and wrong portlibs selection.
- [x] Mitigation recorded: strict dependency guards, artifact timestamp, size, and SHA-256.

## Validation Checklist
- [x] Complete AirPlay host suite exits 0.
- [x] mDNS lifecycle smoke exits 0.
- [x] Strict Switch trace build exits 0 and produces `NX-Cast.nro`.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] Next run expects secondary pair-verify status 200 followed by `/play` or type-110 SETUP rather than immediate TEARDOWN.

## Implementation Notes
The complete host suite and mDNS lifecycle smoke passed. The strict trace build used `/var/folders/yw/ht1sm8j503bdv0_t8s0gkt4c0000gn/T/nxcast-portlibs.f0OCZL/opt/devkitpro/portlibs/switch` with all strict dependency guards enabled. The generated NRO is 25,600,698 bytes, timestamped 2026-08-14 01:10:53 +1200, with SHA-256 `42356384c22fb42f92f0f5e8f8acb508194a676c47846923a3b40ec79a9858b3`. Real-device continuation past secondary verification remains the next validation.

## Files Changed
- `NX-Cast.nro` (generated, ignored build artifact)
- `NX-Cast.elf` (generated, ignored build artifact)
- `plans/2026-08-14-airplay-secondary-verification/plan.md`
- `plans/2026-08-14-airplay-secondary-verification/steps/step-3.md`
