# Plan: Fix nvtegra H.264 long-term references

> Status: ACTIVE
> Created: 2026-10-03
> Last Updated: 2026-10-03

## Goal
Correct the proven long-term reference indexing defect and build a reproducible FFmpeg package and NX-Cast test binary without changing playback architecture.

## Assumptions
- Hardware playback must be confirmed on Switch after the dependency is rebuilt.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] Long-term references come from long_ref, not short_ref.
- [x] FFmpeg package revision 4 retains Matroska and libnx random support.
- [x] Local install, Docker and dependency fetch identify the same package.
- [x] NX-Cast links the corrected library.
### Non-goals
- Protocol, UI, network or state-machine changes; software decoding fallback.
### Edge Cases
- A long-term reference exists while the same short-term reference slot is empty.

## Design Decisions
None — no design-sensitive changes. User approved the targeted hard-decoder fix.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Patch, rebuild and wire the corrected dependency. |
| Step 2 | `steps/step-2.md` | PENDING | Publish dependency after authorization and install globally with user sudo. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Syntax | `bash -n scripts/*switch_ffmpeg_airplay.sh` | Existing shell helpers | yes |
| Dependency build | `make build-airplay-ffmpeg` | Makefile | yes |
| Dependency verification | `scripts/verify_switch_ffmpeg_airplay.sh PREFIX` | Existing verifier | yes |
| App build | `make full-trace-build BUILD_JOBS=4` | Makefile / VS Code task | yes |
| Diff | `git diff --check` | Git | yes |

## Context & Learnings
### Key Decisions
- Keep the upstream pinned recipe and add one small source patch, retaining existing fixes.
### Gotchas & Warnings
- Never overwrite package revision 3; a new revision and checksum are required.
- Global portlibs are root-owned; preserve the crash ELF and use an extracted dependency prefix if installation requires user sudo.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `scripts/build_switch_ffmpeg_airplay.sh` | Pinned source build | Read |
| `scripts/fetch_switch_ffmpeg_airplay.sh` | Pinned binary download | Read |
| `scripts/install_switch_ffmpeg_airplay.sh` | Package installation | Search |
| `Dockerfile` | Shared CI dependency | Search |
| `.github/workflows/toolchain.yml` | Content-addressed toolchain cache | Read |
### Verified Facts
- Crash module EC721A2BAE5D32D62CCB0DC782B6A0F365B994BF matches the saved ELF.
- PC 0x959ff4 in nvtegra_h264_start_frame dereferences a null picture pointer at offset 112.
- Pinned upstream patch checks long_ref[i] but inserts short_ref[i]; local disassembly confirms the same operation.
- Existing build script enables Matroska and applies the libnx random patch.
- Package revision 4 was built and verified; SHA-256 is bc6068b8dfee02356aa571fcc126143bb718d9b900d61f4813190039c82a81d6.
- Extracted actual reference-list code fails the long-only case before patch and passes empty/short/long/sparse/mixed cases after patch.
- Full Trace app link map selects the revision-4 libavcodec.a from artifacts/toolchain/ffmpeg/revision-4; disassembly stores the long_ref pointer directly.
- Final app Build ID is e3adfa2aa83a561e9f9057904fbc336c9abec810; matching ELF is saved in logs/NX-Cast-nvtegra-long-ref-fixed.elf.
- sudo -n fails because a password is required; no global installation was performed. No remote publication or code push has been performed.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-03 | Step 1 | Started narrowly scoped dependency repair. |
| 2026-10-03 | Step 1 | Completed patch, negative/positive regression checks, package build, cached checksum validation and verified app link. |
| 2026-10-03 | Step 2 | Asked for separate dependency-release authorization; global installation requires user sudo. Hardware validation remains pending. |
