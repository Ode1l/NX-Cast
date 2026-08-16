# Step 3: Validate Complete AirPlay Media Build

> Status: COMPLETED
> Created: 2026-08-13

## Goal
Validate host behavior, dependency symbols, and the strict Switch release artifact as one coherent delivery.

## Prerequisites
- Step 1 host ALAC bridge tests completed.
- Step 2 custom Switch FFmpeg package and strict guard completed.
- The existing dirty worktree remains preserved.

## Deliverables
- Recorded test and build evidence for ALAC, Matroska, and Switch linkage.
- Updated plan status and implementation log.
- After this step: the resulting NRO is ready for an iPhone ALAC-then-mirror test.

## Plan
- [x] `bash` `make test-airplay` — run the complete host AirPlay suite.
- [x] `bash` inspect installed or staged Switch FFmpeg symbols — verify decoder and muxer.
- [x] `bash` `make clean && make RELEASE_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1 release-build` — produce strict NRO.
- [x] `read` build output and generated artifacts — confirm no fallback backend and plausible NRO size.
- [x] `edit` plan files — record evidence, deviations, exact files, and final status.

## Quality Checklist
- [x] Evidence-before-edit: all changed paths reviewed with `git diff --check` and targeted diff reads.
- [x] Existing pattern / reuse checked: no duplicate media path introduced.
- [x] Contract understood: host and Switch builds use equivalent codec/container behavior.
- [x] Risk reviewed: device-only behavior remains the residual risk.
- [x] Mitigation recorded: exact next-device log markers documented in final report.

## Validation Checklist
- [x] `git diff --check` exits 0.
- [x] Strict Switch release build exits 0 using a rootless staged portlibs tree.

## Test Checklist
- [x] `make test-airplay` — all pass.
- [x] Package symbol checks — all pass.

## Implementation Notes
- Re-ran the complete host suite after all toolchain and packaging changes; ALAC negotiation, Matroska demux verification, runtime audio-before-video, pairing, mDNS, HLS, ownership, diagnostics, and safety tests passed.
- Copied the installed Switch portlibs tree to a temporary root, overlaid `switch-ffmpeg-7.1-2`, and verified the required symbols before linking. This provided the same static-link contract as package installation without requiring an interactive sudo password.
- The strict release build completed with libmpv/deko3d, libsodium Ed25519, PlayFair, ALAC, H.264 parser, and Matroska muxer requirements active.
- The resulting NRO is 25,572,026 bytes. The final 19,810,075-byte SD zip passed the strict attestation and contains exactly one NRO at `switch/NX-Cast/NX-Cast.nro` plus the preinstalled IPTV `sources.txt`.
- Docker itself was not run because it is not installed by user choice; the Dockerfile path uses the same package builder and symbol checks proven locally, and both workflow YAML files parse successfully.
- Residual risk is device-only AirPlay interoperability. The next iPhone run should confirm `ct=2` no longer logs `stage=format`, then show `stage=open`, bridge packet counters, player load, and decoded H.264/ALAC output.

## Files Changed
- `plans/2026-08-13-airplay-alac-matroska/plan.md`
- `plans/2026-08-13-airplay-alac-matroska/steps/step-3.md`
