# Plan: Switch FFmpeg random source

> Status: COMPLETED
> Created: 2026-09-25
> Last Updated: 2026-09-25

## Goal
Build and distribute a Switch FFmpeg package whose `av_random_bytes` uses libnx instead of the crashing generic fallback.

## Assumptions
- A package rebuild can run in this environment; global installation may require an interactive sudo password.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] The pinned FFmpeg source receives a reviewed Switch RNG patch during package build.
- [x] The package verifier rejects the old archive and accepts the patched one.
- [x] Local and Docker/CI packaging select the same patch and package release.
- [x] Build and host tests pass; target link awaits global package installation, and device playback is left for user testing.

### Non-goals
- Changing AirPlay protocol or returning a constant/randomly seeded fallback.

### Edge Cases
- Building against an already installed old FFmpeg must fail verification instead of silently passing.

## Design Decisions
None — no design-sensitive changes.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Patch the pinned FFmpeg build, package verification, and CI image path, then validate. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Script syntax | `bash -n scripts/build_switch_ffmpeg_airplay.sh scripts/install_switch_ffmpeg_airplay.sh scripts/verify_switch_ffmpeg_airplay.sh` | Bash | Yes |
| Package build | `make build-airplay-ffmpeg NXCAST_FFMPEG_JOBS=4` | `makefile` | Yes |
| Package verifier | `scripts/verify_switch_ffmpeg_airplay.sh <extracted-prefix>` | Project verifier | Yes |
| App test | `make test-airplay` | `makefile` | Yes |
| Target build | `make full-trace-build BUILD_JOBS=4` | `makefile` | If installation succeeds |
| Diff check | `git diff --check` | Git | Yes |

## Context & Learnings
### Key Decisions
- Use a source patch in the pinned package recipe instead of app-level symbol interposition; FFmpeg remains the owner of its RNG implementation.

### Gotchas & Warnings
- `libnx` provides `randomGet`, but existing FFmpeg attempts `/dev/urandom` then a PMCCNTR fallback.
- Pre-existing worktree changes in AirPlay and README must remain untouched.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `scripts/build_switch_ffmpeg_airplay.sh` | Pinned package recipe builder | Read script |
| `scripts/verify_switch_ffmpeg_airplay.sh` | Archive validator | Read script |
| `scripts/install_switch_ffmpeg_airplay.sh` | Local install | Read script |
| `Dockerfile` | CI toolchain image | Read file |
| `.github/workflows/toolchain.yml` | Image cache key | Read file |

### Verified Facts
- Report Module ID matches local ELF and PC is inside FFmpeg `av_get_random_seed` on `mrs pmccntr_el0` — crash report and disassembly, 2026-09-25.
- Pinned FFmpeg `random_seed.c` uses `/dev/urandom` before generic fallback — upstream source and binary disassembly, 2026-09-25.
- libnx declares `randomGet(void *, size_t)` — installed header, 2026-09-25.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-25 | 1 | Built and verified `switch-ffmpeg-7.1-3`; old installed archive and release preflight fail as intended; `make test-airplay`, Bash syntax, and diff check pass. Target link is pending privileged package installation. |
