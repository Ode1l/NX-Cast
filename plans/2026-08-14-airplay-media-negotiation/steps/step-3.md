# Step 3: Validate Switch Media Negotiation

> Status: COMPLETED
> Created: 2026-08-14

## Goal
Produce a strict trace build and a precise real-device validation contract without changing DLNA/IPTV behavior.

## Prerequisites
- Steps 1 and 2 completed with host regression coverage.
- AirPlay-capable Switch FFmpeg and libsodium are available to the selected toolchain.
- Files to modify: plan records only unless validation exposes a scoped defect.

## Deliverables
- Host suite, discovery smoke, strict Switch trace build, and diff hygiene pass.
- Real-device next-run expectations distinguish screen mirroring (`type=110`) from video-app URL/HLS (`/play`).
- After this step: the generated NRO is ready for user upload and one focused trace run.

## Plan
- [x] `bash` `make test-airplay` — rerun host regression suite after the complete patch.
- [x] `bash` `python3 scripts/smoke_airplay_mdns.py` — verify effective discovery response.
- [x] `bash` `source /opt/devkitpro/switchvars.sh && make TRACE_MEDIA=1 TRACE_INPUT=1 TRACE_AIRPLAY=1 NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 NXCAST_REQUIRE_AIRPLAY_MUXER=1 -j4` — build the strict trace NRO.
- [x] `bash` `git diff --check && git status --short` — verify patch hygiene and preserve unrelated dirty work.

## Quality Checklist
- [x] Evidence-before-edit: N/A — verification-only; no task source files changed in this step.
- [x] Existing pattern / reuse checked: `.vscode/tasks.json` trace flags and `makefile` strict dependency guards.
- [x] Contract understood: no claim of real-device success until a new Switch trace contains `/play` or type-110 SETUP and media packets.
- [x] Risk reviewed: local package selection and false-positive host-only success.
- [x] Mitigation recorded: strict muxer/Ed25519 guards plus explicit real-device trace markers.

## Validation Checklist
- [x] `make test-airplay` exits 0.
- [x] `python3 scripts/smoke_airplay_mdns.py` exits 0.
- [x] Strict Switch trace build exits 0 and produces `NX-Cast.nro`.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] Real-device follow-up expects `/play` from a video-app cast or `streams=... mirror=1` plus `client-connected` from Control Center screen mirroring.

## Implementation Notes
The complete host suite and mDNS smoke passed after the compatibility changes. The strict Switch build used the staged AirPlay-capable portlibs at `/var/folders/yw/ht1sm8j503bdv0_t8s0gkt4c0000gn/T/nxcast-portlibs.f0OCZL/opt/devkitpro/portlibs/switch` and enforced libmpv, deko3d, Ed25519, and AirPlay muxer requirements. The generated `NX-Cast.nro` is 25,600,698 bytes with SHA-256 `341741d4ab3d145f7d92664b80b43c9d98b207d13f4cbe698d3dea655d580d1e`. Real-device media success remains a follow-up validation, not a completed claim.

## Files Changed
- `NX-Cast.nro` (generated, ignored build artifact)
- `NX-Cast.elf` (generated, ignored build artifact)
- `plans/2026-08-14-airplay-media-negotiation/plan.md`
- `plans/2026-08-14-airplay-media-negotiation/steps/step-3.md`
