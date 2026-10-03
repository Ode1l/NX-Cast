# Step 1: Prepare and validate 0.3.2

> Status: COMPLETED
> Created: 2026-10-04

## Goal
Produce a normal 0.3.2 release build with updated notes and the complete SD installation ZIP.

## Prerequisites
- Version, toolchain, packaging and pending changes inspected; user approves release of tested sound fixes.

## Deliverables
- Version/notes/changelog updates, explicit trace-free release recipe and verified local package.

## Plan
- [x] Edit makefile and DLNA fallback version; update release notes, development log and README tag examples.
- [x] Add dedicated FFmpeg dependency notes containing pins, provenance and installation distinction.
- [x] Review accumulated diff, run host/UI tests, validate workflow/scripts and FFmpeg.
- [x] Move shared mpv node helpers outside the diagnostics-only guard: clean release build exposed an accidental Trace dependency in audio track selection.
- [x] Build with revision 4, package SD ZIP and check version, presets and trace configuration.

## Quality Checklist
- [x] Evidence-before-edit: target reads and version/release searches performed.
- [x] Reuse: central APP_VERSION, package_release.sh and pinned FFmpeg downloader retained.
- [x] Contract: no application behavior changes beyond formal diagnostics/version and required helper availability.
- [x] Risk: accidental old FFmpeg/Trace build or missing private-file exclusions.
- [x] Mitigation: staged prefix, clean release rebuild, build configuration and ZIP inspection.

## Validation Checklist
- [x] Scripts/YAML/whitespace checks pass.
- [x] Release build and packaging exit 0.

## Test Checklist
- [x] make test-airplay and scripts/test_ui.sh pass.
- [x] NACP/Home 0.3.2, sources.txt intact and no trace/sensitive files.

## Implementation Notes
Initial normal release build failed because libmpv_observe_map_value and libmpv_observe_node_int64 were compiled only under NXCAST_RUNTIME_OBSERVABILITY. The tested Full Trace build included them; audio track selection requires them in all builds. Fix only helper visibility, not playback semantics, and re-run the clean release build.

- Tests: make test-airplay exit 0 (/tmp/nxcast-032-airplay-tests.log); sh scripts/test_ui.sh exit 0 (/tmp/nxcast-032-ui-tests.log).
- Build: make release-build RELEASE_JOBS=4 with staged revision-4 PORTLIBS, exit 0 (/tmp/nxcast-032-release-build.log), no warnings/errors. Link map uses revision-4 libavformat/libavcodec/libavutil; staged codec/util archives byte-match package contents.
- FFmpeg verifier, shell syntax, YAML parsing and git diff --check passed. Recipe patch hashes and package .PKGINFO verified.
- Configuration: normal; trace-media/input/airplay all 0; ImGui/libmpv/deko3d/Ed25519/muxer enabled. NACP version and Home binary string both 0.3.2.
- Package: scripts/package_release.sh exit 0; ZIP has exactly one directory-local NRO, fonts, DLNA files, sources.txt and licenses with no generated secrets. Packaged presets SHA256 ec1665102353b051567c0551cdf1a30f583974b1574ce9e686c006a201917a63 matches source.
- Local ZIP SHA256 b78881f1eb780ebf18efdf7bb073d44ab0071c01f7f5a0b5783a556decc85cf4; NRO SHA256 d73be8aefe3999f184e014d7803848099a653aebb1bdeb258a426d50e4bafe96. Snapshot artifacts/NX-Cast-v0.3.2-local-sdmc.zip; ELF logs/NX-Cast-v0.3.2-local-release.elf, build ID 2bc35125e51679cc10bebe3da87c2207ae2c8987.
- Hardware acceptance of mirror audio is user-reported; no new audio/video behavior was introduced while preparing this release.

## Files Changed
- makefile
- source/protocol/dlna/server_info.c
- source/player/backend/libmpv.c (shared helper visibility)
- .github/release-notes.md
- .github/ffmpeg-release-notes.md
- CHANGELOG.md
- README.md (version tag example)
- plans/2026-10-04-release-0-3-2/plan.md and steps/step-1.md
- Previously accumulated user-tested source/toolchain/Continuous changes are carried forward and will be included in the step-2 release commit; their own task records remain intact.
