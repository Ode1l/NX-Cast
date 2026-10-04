# Plan: Prepare pinned FFmpeg automatically before builds

> Status: COMPLETED
> Created: 2026-10-04
> Last Updated: 2026-10-04

## Goal
Make existing local build/rebuild/release commands prepare the pinned GitHub FFmpeg dependency automatically, without network access or sudo when already installed.

## Assumptions
- User authorizes implementing the previously proposed automatic installation using the existing global devkitPro package route.
- devkitPro and other media dependencies remain prerequisites; this task automates FFmpeg only.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] Normal, trace and release build entries install missing/outdated FFmpeg before Make probes media capabilities.
- [x] Correct package version and required files skip download, installation and sudo.
- [x] Existing GitHub URL, SHA-256 verification and atomic download cache remain in use; failures stop the build.
- [x] Clean, host tests and dry runs cause no automatic package installation.
- [x] Root builds can use the installed-package fast path without sudo; custom prefixes have an explicit opt-out.
- [x] README instructions describe the new behavior and local validation passes.
### Non-goals
- Playback code, release tag/assets, new CI test gates, automatically installing the entire SDK.
### Edge Cases
- Make parse-time checks must occur after preparation, and dependency version changes must invalidate stale build objects.
- Broken package files, download/installation failure, custom prefixes and parallel Make goals must not bypass preparation or report false success.

## Design Decisions
None - implement the fixed-version automatic-install flow already proposed in conversation and requested by the user.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | steps/step-1.md | COMPLETED | Wire idempotent dependency preparation into build entry points and verify |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Installer/build ordering | python3 scripts/test_ffmpeg_install.py | Focused isolated command fixtures | yes |
| Shell syntax | bash -n scripts/install_switch_ffmpeg_airplay.sh scripts/fetch_switch_ffmpeg_airplay.sh | Bash scripts | yes |
| Current package no-op | make install-airplay-ffmpeg | Installed switch-ffmpeg 7.1-4 | yes |
| Real build | make dev-build BUILD_JOBS=4 | VS Code entry | yes |
| Formatting/docs | git diff --check; documentation link/syntax inspection | Existing docs workflow | yes |

## Context & Learnings
### Key Decisions
- Reuse the fetch script as package-name authority, the package manager database for version checks, and existing cache/checksum behavior.
- A small outer Make dispatch prepares dependencies before re-entering the existing Makefile. No package side effects occur during Make parsing.
- Use a package identifier in the existing build configuration stamp so a future pinned revision rebuilds stale objects.
### Gotchas & Warnings
- Global package writes still require sudo on a normal user account; the agent will not ask for or handle passwords.
- Keep a custom-toolchain opt-out; automatic global installation cannot provision arbitrary staging prefixes.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| makefile | Build dispatch and configuration stamp | Read parse-time checks, recursive dev/trace/release/all rules |
| scripts/install_switch_ffmpeg_airplay.sh | Existing global installation | Always installs and rejects root before any current-version check |
| scripts/fetch_switch_ffmpeg_airplay.sh | Pinned package/cache | Version 7.1-4 and fixed SHA-256; atomic downloads |
| .vscode/tasks.json, scripts/run_nxlink.sh | Existing callers | Tasks use Make entries; nxlink can use plain make |
| Dockerfile, .github/workflows/toolchain.yml | CI reuse | Image already contains revision 4; fetch script is hashed/copied |
| README.md, README_CN.md, docs/ffmpeg-mpv-toolchain.md | User instructions | Manual dependency installation currently required |
### Verified Facts
- dkp-pacman -Q switch-ffmpeg now reports 7.1-4 on this machine.
- Make evaluates required media capability checks before running target prerequisites.
- The existing build configuration stamp handles full object invalidation and is reusable.
- Ten isolated installer/Make tests pass, including missing/outdated package installation, offline current-version skip, damaged cache, failed/incomplete installation, custom prefix, all nine build entries, non-build goals and dry run. Tests never write to the real SDK or access the network.
- Real make install-airplay-ffmpeg skipped installation for local revision 4. make dev-build BUILD_JOBS=4 succeeded with no compiler warnings/errors. A second invocation skipped installation and reported Nothing to be done for all.
- Shell syntax, git diff --check and 22 local documentation links passed. No new test gate was added to CI.
- Published implementation commit 6dd7127 to main. GitHub run 37169395387 completed successfully, including root-container dependency preparation, existing tests, strict release-build, packaging and Continuous publication: https://github.com/Ode1l/NX-Cast/actions/runs/37169395387 .

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-04 | Step 1 | Added recipe-based Make preparation, package-version no-op installation, shared package metadata and build signature; verified fixtures and real incremental build; updated bilingual README/toolchain guidance. |
