# Plan: Global Matroska Toolchain

> Status: COMPLETED
> Created: 2026-08-16
> Last Updated: 2026-08-16

## Goal
Provide one verified Switch FFmpeg package installation path that enables the Matroska muxer for local builds and both GitHub build and release workflows.

## Assumptions
- The pinned wiliwili FFmpeg recipe remains the supported base because NX-Cast already depends on its nvtegra-enabled Switch port.
- Local installation may require the developer to enter a sudo password; automation must not try to bypass that boundary.
- GitHub Actions continues to build through the repository Dockerfile.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] A single script verifies ALAC decoder, H.264 parser, and Matroska muxer in an installed Switch FFmpeg prefix.
- [x] A local Make target builds and globally installs the pinned package under `/opt/devkitpro/portlibs/switch`.
- [x] Docker, GitHub build, and GitHub release fail before compiling NX-Cast if the global FFmpeg install lacks any required symbol.
- [x] `make release-build` no longer needs a temporary `LDFLAGS` override after installation.

### Non-goals
- Enabling every FFmpeg muxer or replacing the pinned wiliwili FFmpeg/mpv stack.
- Uploading the custom FFmpeg package as a GitHub release asset.

### Edge Cases
- `dkp-makepkg` refuses root builds, while the resulting local package must be installed as root in Docker and with sudo on macOS.
- CI must install only a local package with `dkp-pacman -U`; it must not download packages from devkitPro repositories.

## Design Decisions
None — no design-sensitive changes.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add reusable global FFmpeg verifier and local install entry points |
| Step 2 | `steps/step-2.md` | COMPLETED | Wire the same verifier into Docker and both GitHub workflows |
| Step 3 | `steps/step-3.md` | COMPLETED | Update documentation and validate local/CI-equivalent contracts |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Shell syntax | `bash -n scripts/build_switch_ffmpeg_airplay.sh scripts/install_switch_ffmpeg_airplay.sh scripts/verify_switch_ffmpeg_airplay.sh` | Existing Bash toolchain scripts | Yes |
| Make contract | `make verify-airplay-ffmpeg` | `makefile` release capability gate | Yes after global installation; expected diagnostic before installation |
| Host tests | `make test-airplay` | Existing AirPlay test aggregate | Yes |
| Release build | `make RELEASE_JOBS=4 release-build` | Existing release target | Yes after global installation or CI image verification |
| Workflow lint | `actionlint .github/workflows/build.yml .github/workflows/release.yml` | GitHub Actions configuration | If `actionlint` is installed |

## Context & Learnings
### Key Decisions
- Treat Matroska as a compile-time FFmpeg capability, not a runtime plugin: install the complete custom `switch-ffmpeg` package globally.
- Keep package construction unprivileged and installation privileged, matching `dkp-makepkg` and pacman constraints.
- Centralize symbol verification so Make, Docker, and CI cannot drift.

### Gotchas & Warnings
- `/opt/devkitpro/portlibs/switch/lib/libavformat.a` currently lacks `ff_matroska_muxer`, so this machine still needs the one-time privileged package install.
- The repository worktree contains unrelated in-progress AirPlay changes; this task must not revert them.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `scripts/build_switch_ffmpeg_airplay.sh` | Builds pinned custom FFmpeg package | `read` confirms `--enable-muxer=matroska` and symbol checks |
| `Dockerfile` | CI toolchain image | `read` confirms unprivileged package build followed by global `dkp-pacman -U` |
| `.github/workflows/build.yml` | Branch/PR and continuous release build | `read` confirms Dockerfile-based release build |
| `.github/workflows/release.yml` | Tagged release build | `read` confirms the same Dockerfile-based release build |
| `makefile` | Local and release build contract | `rg` confirms `NXCAST_REQUIRE_AIRPLAY_MUXER` checks global portlibs archives |
| `README.md` and `docs/ffmpeg-mpv-toolchain.md` | Developer installation instructions | `rg` confirms manual two-command installation is documented |

### Verified Facts
- The pinned package already enables only the Matroska muxer in addition to the wiliwili recipe — verified by `read scripts/build_switch_ffmpeg_airplay.sh`, 2026-08-16.
- Both GitHub workflows build the repository Dockerfile and run `make release-build` inside it — verified by `read .github/workflows/build.yml .github/workflows/release.yml`, 2026-08-16.
- The Dockerfile installs the locally built package with `dkp-pacman -U`, not `dkp-pacman -S` — verified by `read Dockerfile`, 2026-08-16.
- The current local Switch `libavformat.a` lacks `ff_matroska_muxer` — verified by `aarch64-none-elf-nm`, 2026-08-16.
- The pinned package rebuild completes as `switch-ffmpeg 7.1-2` and the extracted package passes the shared ALAC/H.264/Matroska verifier — verified by `make NXCAST_FFMPEG_JOBS=4 build-airplay-ffmpeg`, 2026-08-16.
- The full host AirPlay aggregate passes after the toolchain changes — verified by `make test-airplay`, 2026-08-16.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-16 | Step 1 | Added shared target-archive verification, one-command privileged installation, and Make entry points; syntax and dry-run checks pass. |
| 2026-08-16 | Step 2 | Reused the verifier after Docker global package installation and before build/release compilation; confirmed CI paths use only local pacman installs. |
| 2026-08-16 | Step 3 | Documented compile-time/global semantics; completed host tests, package rebuild, verifier success/failure fixtures, and diff checks. |
