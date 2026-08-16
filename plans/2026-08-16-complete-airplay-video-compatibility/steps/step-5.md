# Step 5: Release Verification And Documentation

> Status: COMPLETED
> Created: 2026-08-16

## Goal
Validate the completed AirPlay video subset across host and Switch builds and leave a precise real-device test and support contract.

## Prerequisites
- Steps 1-4 completed with no known test failures.
- Files to modify: AirPlay compatibility/development documentation, README support wording, and build scripts only if validation proves a defect.
- Real iPhone/Switch execution is performed by the user after artifacts are produced.

## Deliverables
- All host tests and strict trace/release Switch builds pass.
- Documentation distinguishes implemented, hardware-verified, experimental, and explicitly unsupported capabilities.
- A compact hardware matrix covers Control Center mirroring, app URL/HLS casting, audio, stop/reconnect, and DLNA/IPTV regression.

## Plan
- [x] `bash` `git diff --check` and `make test-airplay`.
- [x] `bash` `make full-trace-build BUILD_JOBS=4` and `make release-build RELEASE_JOBS=4`.
- [x] `edit` compatibility/development documentation and README with evidence-based support status.
- [x] `bash` rerun affected documentation/build checks and inspect final diff for unrelated changes.

## Quality Checklist
- [x] Evidence-before-edit: all prior step verification logs and current build configuration
- [x] Existing pattern / reuse checked: README support table and AirPlay diagnostic documents
- [x] Contract understood: host/build success is not presented as real-device interoperability proof
- [x] Risk reviewed: release regression, misleading capability claims, dependency mismatch
- [x] Mitigation recorded: strict builds, explicit hardware-pending labels, regression checklist

## Validation Checklist
- [x] `git diff --check` exits 0
- [x] Strict trace and release Switch builds exit 0

## Test Checklist
- [x] `make test-airplay` passes
- [x] Real-device checklist is ready and marked pending rather than falsely passed

## Implementation Notes
The installed Switch FFmpeg already had ALAC and the H.264 parser but lacked the Matroska muxer. The pinned package recipe produced and symbol-verified `switch-ffmpeg-7.1-2`; release verification linked its archive from a temporary path to avoid changing global pacman state. Normal local release builds still require installing that package. Hardware status remains pending and is not inferred from host or cross-build success.

## Files Changed
- `README.md`
- `docs/README.md`
- `docs/AIRPLAY_DEVELOPMENT.md`
- `docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md`
- `scripts/build_switch_ffmpeg_airplay.sh`
- `scripts/package_release.sh`
- `makefile`
