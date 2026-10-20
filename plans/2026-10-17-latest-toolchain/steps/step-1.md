# Step 1: Complete media source recipes and latest toolchain caching

> Status: IN_PROGRESS
> Created: 2026-10-17

## Goal
Build a complete source-rebuilt media toolchain keyed by the latest official base image and local recipes.

## Prerequisites
- User approved latest-base rebuild strategy.
- Complete source recipe for the currently used deko3d mpv binary, or an explicitly approved replacement migration.

## Deliverables
- Source-built custom FFmpeg/mpv image, automatically reused until inputs change.
- Updated container entry points and CI documentation.

## Plan
- [x] Read existing Dockerfile, workflows, scripts, and upstream media recipes.
- [x] Obtain a candidate replacement source implementation and compile it independently (hardware equivalence not claimed).
- [x] Implement source builds and base-aware cache resolution in an opt-in workflow.
- [x] Build image and application; record required hardware verification.

## Quality Checklist
- [x] Evidence-before-edit: read build entry points and searched existing recipes.
- [x] Existing pattern / reuse checked: FFmpeg source builder can be reused.
- [x] Contract understood: no application-only rebuild of dependencies.
- [x] Risk reviewed: replacing a media fork can regress rendering and audio.
- [x] Mitigation recorded: do not activate incomplete release configuration.

## Validation Checklist
- [x] Local FFmpeg 7.1-4 rebuilt with GCC 16.1.0; feature verifier passed.
- [x] Local mpv compiled against the rebuilt FFmpeg; NX-Cast candidate NRO linked.
- [x] Local libuam source rebuild and full source-media application link passed on 2026-10-20.
- [x] Shell syntax, YAML parse/structure, invalid job-count rejection, and git diff whitespace checks passed.
- [x] Complete Linux toolchain image build (39353197399 attempt 1).
- [x] NX-Cast release build against new image (both attempts).

## Test Checklist
- [ ] Confirm unchanged inputs reuse image; changed base rebuilds media.
- [x] Unchanged-input cloud rerun explicitly skipped image build and succeeded in 64 seconds. Changed-base behavior remains verified by key construction, not a second base-image experiment.
- [x] User reports candidate hardware testing passed; individual test coverage was not enumerated.

## Implementation Notes
Cloud run 39353197399 at fbf6452 passed both attempts: full image/application job 10m01s, cached job 1m04s. Linux artifact hardware testing remains outstanding before production promotion. Candidate-only documentation updated; no production workflow changes.

Cloud run 39352647588 compiled libuam successfully but failed before FFmpeg compilation because makepkg requires fakeroot on Linux. Added fakeroot to the candidate image's apt dependencies; production images remain unchanged. Retry and cache validation are pending.

User confirmed candidate works on hardware and authorized commit/push and candidate Actions execution. Publish only toolchain files on codex/toolchain-candidate; leave unrelated working-tree changes uncommitted. Production workflows remain unchanged.

2026-10-20: Built pinned libuam with GCC 16.1.0 in an isolated sysroot and linked NX-Cast-source-candidate.nro successfully. The map confirms isolated libuam/mpv/FFmpeg archives. macOS Bison was too old; Homebrew Bison 3.8.2 resolved parser generation without source changes. Python build tools are in a project-local venv. Candidate Dockerfile now preserves libuam license and source metadata. No existing installed media libraries were replaced. Linux container execution and hardware tests remain unperformed; no commit or push.

2026-10-18: Candidate NRO at artifacts/toolchain/NX-Cast-candidate.nro; SHA-256 35c09f04d1f21781bd53adf44c343a5f6f0c9c1bdbab739d8cb012cd4120c8ef. Matching ELF SHA-256 e9dcbb47b25faaee4c2d1ca48b68893e4101db41d4fb43dfb7e6b591e3afe528.
Local build used libnx 4.12.0-1, devkitA64 r30-1, and existing system libuam (not recompiled locally). The candidate Dockerfile rebuilds libuam too, but that Linux path is unverified. Missing container runtime and no remote publication/run are the remaining validation blockers. Production CI remains untouched.
Fixed two build configuration issues without application source edits: select Horizon rather than the host OS, and enable static pkg-config dependency discovery. Local app build clears inherited CPPFLAGS to prevent system headers shadowing the isolated sysroot. Link map verified candidate libraries.

User authorized independent source investigation and candidate builds. Do not activate the candidate in release workflows before validation. Resume with isolated artifacts, leaving system-installed libraries untouched.

Blocked before implementation: Wiliwili's deko3d PKGBUILD references an absent mpv.patch. The ordinary mpv recipe is not a deko3d substitute. Alternative averne/mpv source exists but is not proven equivalent to the installed release dependency. No build configuration or playback code changed.

## Files Changed
- plans/2026-10-17-latest-toolchain/plan.md
- plans/2026-10-17-latest-toolchain/steps/step-1.md
- scripts/build_switch_mpv_candidate.sh
- scripts/mpv_switch_cross.patch
- scripts/libuam_switch_cross.ini
- Dockerfile.candidate
- .github/workflows/toolchain-candidate.yml
- docs/toolchain-candidate.md
- docs/ci-toolchain.md
