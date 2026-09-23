# Plan: NX-Cast 0.3.1 Release

> Status: ACTIVE
> Created: 2026-09-23
> Last Updated: 2026-09-23

## Goal
Publish the current UI/IPTV work as a complete NX-Cast v0.3.1 SD-card ZIP with consistent version metadata.

## Assumptions
- The current `new-ui` worktree changes are intended release content, as the user explicitly requested packaging after approving the UI.
- Pushing annotated tag `v0.3.1` triggers the repository's formal GitHub Release workflow.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [ ] NACP, in-app UI, and DLNA server identity report `0.3.1`.
- [ ] Changelog and GitHub release notes describe the 0.3.1 changes.
- [ ] Release build and the complete SD package pass validation.
- [ ] Commit and push the release contents and `v0.3.1` tag; verify GitHub publishes the package.

### Non-goals
- No additional UI or IPTV behavior changes beyond the already-present worktree changes.
- No private signing certificate or runtime AirPlay identity material is added to the package.

### Edge Cases
- Ensure the tag does not already exist remotely and ensure the archive contains the full install folder, not only the NRO.

## Design Decisions
None — version and packaging metadata only.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | IN_PROGRESS | Version, document, build, package, and publish v0.3.1 |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Diff hygiene | `git diff --check` | Git | yes |
| UI tests | `make test-ui` | makefile | yes |
| IPTV data tests | `make test-iptv-data` | makefile | yes |
| Release build | `make RELEASE_JOBS=4 release-build` | makefile | yes |
| SD package | `sh scripts/package_release.sh` | packaging script | yes |
| Published release | `gh release view v0.3.1` | GitHub CLI | yes |

## Context & Learnings
### Key Decisions
- One `APP_VERSION` source drives the NACP and the C/C++ `NXCAST_APP_VERSION` compile definition.
- The formal release workflow is tag-triggered and publishes the complete SD ZIP.
### Gotchas & Warnings
- This checkout is on `new-ui`, and its intended UI/IPTV changes are currently uncommitted; preserve and include them in the release commit.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `makefile` | NACP and compiled version source | inspected APP_VERSION and compiler define |
| `source/player/render/imgui/imgui_overlay.cpp` | Visible UI version | inspected NXCAST_APP_VERSION use |
| `source/protocol/dlna/server_info.c` | DLNA server version fallback | inspected compile-time fallback |
| `CHANGELOG.md` | Version history | inspected Unreleased section |
| `.github/release-notes.md` | GitHub release body | inspected release notes |
| `.github/workflows/release.yml` | Tag-triggered package publishing | inspected workflow |

### Verified Facts
- Remote tag `v0.3.1` does not exist — verified by `git ls-remote --tags origin refs/tags/v0.3.1` on 2026-09-23.
- GitHub CLI is authenticated as `Ode1l` with repository and workflow scopes — verified by `gh auth status` on 2026-09-23.
- UI obtains its displayed version from `NXCAST_APP_VERSION`, compiled from `APP_VERSION` — verified by source inspection on 2026-09-23.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
