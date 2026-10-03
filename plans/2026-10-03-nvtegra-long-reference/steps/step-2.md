# Step 2: Publish and install the corrected dependency

> Status: PENDING
> Created: 2026-10-03

## Goal
Make revision 4 available at the pinned public URL and in the user's global devkitPro installation.

## Prerequisites
- Step 1 completed; verified package exists in artifacts/toolchain/ffmpeg.
- User authorization for a dependency-only GitHub release is pending via the asynchronous question.
- User must perform sudo installation because noninteractive sudo requires a password.

## Deliverables
- toolchain-ffmpeg-7.1-4 dependency release with the pinned package and auditable recipe/patch.
- Global FFmpeg package updated; standard rebuild no longer links revision 3.

## Plan
- [ ] After authorization, publish the dependency without an app release or unrelated code push.
- [ ] Download through the normal fetch script into a fresh cache and validate its checksum.
- [ ] User installs the package with dkp-pacman; verify version and rebuild normally.

## Quality Checklist
- [x] Evidence-before-edit: release pins and package metadata inspected.
- [x] Existing pattern / reuse checked: follow toolchain-ffmpeg-7.1-3 publication.
- [x] Contract understood: public URL must match the pinned digest.
- [x] Risk reviewed: do not publish unrelated dirty worktree code; no sudo password handling.
- [x] Mitigation recorded: separate authorization and dependency-only release.

## Validation Checklist
- [ ] Public fetch validates the pinned SHA-256.
- [ ] Global package is 7.1-4 and regular rebuild uses the fixed library.

## Test Checklist
- [ ] User tests first image, continuous mirroring and rotation; preserve matching ELF if a new crash occurs.

## Implementation Notes
Local test NRO is already built with the corrected extracted archive and Full Trace. Use Upload + nxlink server without rebuild until the global package is installed. Public download URL is prepared but must not be used by CI before the dependency release is published.

## Files Changed
None yet.
