# Plan: Release NX-Cast v0.3.0

> Status: ACTIVE
> Created: 2026-09-19
> Last Updated: 2026-09-19

## Goal
Publish the tested NX-Cast code as GitHub Release `v0.3.0` with reproducible NRO and complete SD-card package assets.

## Assumptions
- The user explicitly approved the current tested AirPlay, DLNA, IPTV, UI, CJK, and logging state for release.
- `airplay` is the release source branch and `main` can be fast-forwarded because it is an ancestor.
- Historical untracked plan directories and runtime logs are development records, not release source inputs.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] Application metadata and changelog identify version `0.3.0`.
- [x] Host tests, strict Release build, and package verification pass.
- [ ] `main`, annotated tag `v0.3.0`, and GitHub Release point to the validated release commit.
- [ ] GitHub Release contains `NX-Cast.nro` and `NX-Cast-sdmc.zip` with intact IPTV presets and fonts.

### Non-goals
- Removing historical local plan directories or rewriting previous commits.
- Claiming non-experimental AirPlay 2, DRM, AWDL, or multi-room support.

### Edge Cases
- Do not tag a Full Trace binary or package runtime secrets, logs, captures, identity, or pairing state.
- Do not publish if CI or release asset verification fails.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Release source | Tag `airplay`; merge to `main` then tag | Fast-forward `main` to validated `airplay`, then tag | yes, inferred from explicit formal-release request and existing main/tag convention |
| Assets | Standalone NRO only; full SD package plus NRO | Publish both existing workflow assets | yes, existing project release contract |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Protect tested work and prepare v0.3.0 metadata. |
| Step 2 | `steps/step-2.md` | COMPLETED | Run release tests, build, and package verification. |
| Step 3 | `steps/step-3.md` | IN_PROGRESS | Commit, merge, tag, push, and verify GitHub Release assets. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Host suite | `make test-airplay` | `.github/workflows/release.yml` | yes |
| Strict Switch build | `make RELEASE_JOBS=4 release-build` | `README.md`, release workflow | yes |
| Package | `NXCAST_MIN_NRO_SIZE=5000000 ./scripts/package_release.sh` | release workflow | yes |
| Diff safety | `git diff --check` and staged secret/artifact inspection | Git/project policy | yes |
| Remote release | `gh run watch` and `gh release view v0.3.0` | GitHub workflow | yes |

## Context & Learnings
### Key Decisions
- GitHub Actions remains the authoritative public artifact builder; the local release build is a pre-tag gate.
- Product files are staged explicitly so unrelated untracked plans and logs cannot enter the release commit.

### Gotchas & Warnings
- The working tree contains substantial tested changes and untracked product source files; broad `git add -A` would also stage historical plan directories.
- A tag push immediately triggers public release automation, so it occurs only after local release/package verification.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `makefile` | NACP application version and strict release target | `rg APP_VERSION` and target read, 2026-09-19 |
| `CHANGELOG.md` | User-facing version history | file read, 2026-09-19 |
| `.github/release-notes.md` | GitHub Release body | file read, 2026-09-19 |
| `.github/workflows/release.yml` | Tagged build and asset upload contract | workflow read, 2026-09-19 |
| `scripts/package_release.sh` | SD package and preset/font/security verification | script read, 2026-09-19 |

### Verified Facts
- `main` is an ancestor of `airplay` with a 0/45 left-right count, so release integration can be a fast-forward — verified by `git rev-list`, 2026-09-19.
- `v0.3.0` is absent locally and remotely — verified by `git tag` and `git ls-remote`, 2026-09-19.
- GitHub CLI is authenticated with repository and workflow scopes — verified by `gh auth status`, 2026-09-19.
- Tagged CI rebuilds the project, runs `make test-airplay`, packages the SD layout, and uploads both required assets — verified by `.github/workflows/release.yml`, 2026-09-19.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-19 | Step 1 | Protected the tested implementation in commit `557c92e` and prepared consistent v0.3.0 metadata and public notes. |
| 2026-09-19 | Step 2 | Passed the full host suite and strict normal-profile Release build; verified the 24 MB NRO and complete 19 MB SD package, including exact IPTV presets and the CJK font. |
