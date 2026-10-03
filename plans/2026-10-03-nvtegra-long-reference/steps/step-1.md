# Step 1: Correct and rebuild the FFmpeg dependency

> Status: COMPLETED
> Created: 2026-10-03

## Goal
Produce revision 4 with the long-term reference indexing fix and relink NX-Cast for hardware testing.

## Prerequisites
- Matched crash report and preserved ELF exist in logs.
- User approved the narrow fix; existing unrelated worktree edits must be preserved.

## Deliverables
- Small source patch, updated dependency recipe/pins and built package.
- Full Trace NRO using the corrected hard-decoder library when environment permits.

## Plan
- [x] Add the long_ref patch and apply it after existing recipe patches.
- [x] Build revision 4 and inspect the corrected source/object.
- [x] Update package name/checksum in fetch/install/Docker/docs and toolchain hash.
- [x] Verify the package and link the app; record any installation/publication limitation.

## Quality Checklist
- [x] Evidence-before-edit: scripts and recipe read; package references searched.
- [x] Existing pattern / reuse checked: use existing source package and fetch helpers.
- [x] Contract understood: H.264 DPB must select the corresponding reference list.
- [x] Risk reviewed: dependency mismatch, stale cache, root-owned installation.
- [x] Mitigation recorded: bump package revision, pin checksum, inspect object, keep saved ELF.

## Validation Checklist
- [x] Shell syntax and git diff checks pass.
- [x] Dependency build and verification pass.
- [x] App compiles against corrected FFmpeg.

## Test Checklist
- [x] Confirm the patched source selects long_ref and compile the actual decoder.
- [ ] Hardware first-frame/rotation/continuous mirroring: pending user test, not claimed as verified.

## Implementation Notes
No playback architecture changes. Public dependency publication and global installation will be reported explicitly.
The first app build deleted the staged dependency under build when changing the trace configuration and therefore linked the old global archive. Link-map inspection caught this before delivery. Move the toolchain cache to artifacts and rebuild outside the app clean directory; final verification must inspect the actual linked archive.
An intermediate rerun was invalidated by editing the shell build script while bash was executing it; the final run used the settled script and completed successfully. The source recipe, checksum validation and dependency verification all pass. Final NRO links the corrected archive; ELF disassembly confirms long_ref selection. No application runtime source was edited in this task. CI only gains a cache-hash input; the targeted regression test is local-only. Root-owned global installation and public dependency publication remain separate pending actions in step 2.

## Files Changed
- scripts/ffmpeg_nvtegra_long_ref.patch
- scripts/build_switch_ffmpeg_airplay.sh
- scripts/fetch_switch_ffmpeg_airplay.sh
- scripts/install_switch_ffmpeg_airplay.sh
- scripts/test_ffmpeg_nvtegra_refs.c
- scripts/test_ffmpeg_nvtegra_refs.sh
- makefile
- Dockerfile (preserved existing unrelated edits)
- .github/workflows/toolchain.yml
- docs/ci-toolchain.md
- docs/ffmpeg-mpv-toolchain.md
