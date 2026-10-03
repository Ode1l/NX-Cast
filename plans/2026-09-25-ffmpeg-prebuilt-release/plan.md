# Plan: Publish and consume a prebuilt Switch FFmpeg package

> Status: COMPLETED
> Created: 2026-09-25
> Last Updated: 2026-09-25

## Goal
Make NX-Cast local builds and CI install one pinned, checksum-verified Switch FFmpeg package from a dedicated GitHub Release instead of rebuilding FFmpeg on each new toolchain image.

## Assumptions
- `toolchain-ffmpeg-7.1-3` is a separate dependency release tag; application `v*` releases remain unchanged.
- The locally built `switch-ffmpeg-7.1-3-any.pkg.tar.zst` is the package to publish, with SHA-256 `fc47e883da3693847e7a845e9de3b235c3449bd9103a046bdb200faf61f48b70`.
- `gh` authentication and repository permissions permit publication after source changes are committed.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] A fixed GitHub Release URL and checksum provide the same FFmpeg package to local developers and Docker CI.
- [x] Corrupt or missing downloads fail before package installation; a valid cached file is reused.
- [x] The pinned source-builder remains available for maintainers.
- [x] The package is published and independently fetched and verified.

### Non-goals
- Replacing Wiliwili's libuam or libmpv packages.
- Adding the FFmpeg binary to Git history or to the application SD-card release.

### Edge Cases
- Existing local package with a wrong digest must not be silently installed.
- The dependency tag must not trigger the application's `v*` release workflow.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Dependency distribution | GitHub Packages/OCI, application release asset, separate dependency release asset | Separate fixed GitHub Release asset, following Wiliwili's `v0.1.0` dependency release pattern | User requested Wiliwili pattern |
| Source build | Automatic CI rebuild, manual maintainer path | Manual `make build-airplay-ffmpeg`; normal local/CI path downloads verified asset | User requested fast GitHub and easy local builds |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add a verified prebuilt-package fetch path for local installation and Docker CI, then test and document it. |
| Step 2 | `steps/step-2.md` | COMPLETED | Commit and publish the dependency asset, then verify the public download and source/artifact association. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Shell syntax | `bash -n scripts/fetch_switch_ffmpeg_airplay.sh scripts/install_switch_ffmpeg_airplay.sh` | Bash | Yes |
| Local fetch | `NXCAST_FFMPEG_PACKAGE_URL=file://... scripts/fetch_switch_ffmpeg_airplay.sh <tempdir>` | New fetch script | Yes |
| Package content | `scripts/verify_switch_ffmpeg_airplay.sh <extracted-prefix>` | Existing verifier | Yes |
| Host suite | `make test-airplay` | `makefile` | Yes |
| Diff check | `git diff --check` | Git | Yes |
| Remote asset | `gh release view toolchain-ffmpeg-7.1-3` and verified download | GitHub CLI | Yes |
| Docker image | `docker build ...` | `Dockerfile` | No: Docker is not installed locally |

## Context & Learnings
### Key Decisions
- Use a dedicated dependency release so app tags and SD-card releases remain independent.
- Pin the binary digest, not just its URL; keep package contents checked by the existing verifier after installation.

### Gotchas & Warnings
- The worktree contains pre-existing README and AirPlay changes; preserve them and stage only task-related changes for publication.
- The current Dockerfile builds FFmpeg from source; a missing public asset would break the new fast path until release publication.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `scripts/build_switch_ffmpeg_airplay.sh` | Source build fallback | Read and previous package build |
| `scripts/install_switch_ffmpeg_airplay.sh` | Local install path | Read script |
| `Dockerfile` | CI toolchain dependency installation | Read file |
| `.github/workflows/toolchain.yml` | CI image cache key | Read file |
| `README.md`, `docs/ffmpeg-mpv-toolchain.md`, `docs/ci-toolchain.md` | Developer instructions | `rg` references and targeted reads |

### Verified Facts
- Wiliwili's Switch build script downloads its custom `.pkg.tar.zst` dependencies from its `v0.1.0` GitHub Release and installs them with `dkp-pacman -U` — Wiliwili GitHub script and release asset API, 2026-09-25.
- NX-Cast Docker currently invokes `build_switch_ffmpeg_airplay.sh` during image creation — `Dockerfile`, 2026-09-25.
- GitHub Actions reuse a GHCR image keyed by toolchain inputs and `release.yml` only matches `v*` tags — workflow files, 2026-09-25.
- The local 7.1-3 package exists and its SHA-256 was measured — `shasum -a 256`, 2026-09-25.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-25 | 1 | Added pinned release fetch for local/Docker installs, updated documentation and cache key; valid, cached, corrupt, and recovered package cases passed, as did archive verification and `make test-airplay`. |
| 2026-09-25 | 2 | Committed dependency path as `598fe45`, published tagged package, fetched public asset with expected SHA-256 and archive symbols, pushed `main`, and observed successful GitHub build run `36118884004` using downloaded package. |
