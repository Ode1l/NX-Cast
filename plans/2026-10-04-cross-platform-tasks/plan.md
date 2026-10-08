# Plan: Cross-platform developer and release commands

> Status: COMPLETED
> Created: 2026-10-04
> Last Updated: 2026-10-04

## Goal
Provide four common VS Code launch workflows on macOS and Windows using shared Bash/Make logic and existing GitHub release automation.
## Assumptions
- Windows has devkitPro MSYS2; its machine is unavailable for native testing.
- Existing uncommitted review fixes belong to this conversation and must be preserved.
## Open Questions
None.
## Spec-Lite
### Acceptance Criteria
- Four launches: rebuild/upload, Full Trace/upload/log, upload only, GitHub release.
- No incremental or AirPlay-off launch; j4 and explicit Trace flags.
- Windows adapter locates MSYS2 Bash; shared commands locate SDK via DEVKITPRO.
- Release uses committed main, APP_VERSION and release notes, never overwrites remote tags or auto-commits.
### Non-goals
- Actual GitHub publishing/push, playback changes, new CI checks, toolchain installation on Windows.
### Edge Cases
- Missing Bash/SDK, Windows drive paths, existing tags, dirty worktree, failed push.
## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Platform logic | Two complete scripts / thin adapter | Shared Bash + PowerShell entry | User approved |
| Publishing | Local binary upload / tag-triggered Actions | Existing tag-triggered Actions | User approved |
## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | steps/step-1.md | COMPLETED | Implement shared commands, adapters, VS Code wiring and isolated checks |
## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Syntax | bash -n scripts/*.sh; JSON parsing | Existing script/config format | yes |
| Regression | python3 scripts/test_dev_workflow.py | Isolated command/release fixtures | yes |
| Dependencies | python3 scripts/test_ffmpeg_install.py | Existing installer fixtures | yes |
| Build | bash scripts/dev.sh build | Shared entry | yes |
| Scope | git diff --check | Git | yes |
## Context & Learnings
### Key Decisions
- Keep task composition in VS Code composite tasks because preLaunchTask accepts a single task name.
### Gotchas & Warnings
- Windows must use devkitPro-compatible MSYS2, not WSL or arbitrary Git Bash.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| .vscode/tasks.json, .vscode/launch.json | Current macOS-only entry points | Read |
| scripts/run_nxlink.sh | Shared uploader with a hardcoded SDK path | Read |
| scripts/install_switch_ffmpeg_airplay.sh | sudo-based installation needs MSYS handling | Read |
| .github/workflows/release.yml | v* tag publication | Read, unchanged |
### Verified Facts
- Release workflow builds and publishes the complete SD ZIP on v* tag pushes.
- release notes currently declare v0.3.2, which already exists; publish must reject reuse.
- Shell syntax, task references, 9 workflow tests and 11 installer tests passed. Git publication tests used local disposable repositories, never GitHub.
- Shared macOS entry completed a full normal build using the installed FFmpeg 7.1-4 package; no download/install required.
- Native Windows/PowerShell execution remains unverified; environment setup, path limitations and commands documented in docs/developer-workflow.md.
- Playback/coordinator code and GitHub workflows unchanged; previous review fixes preserved. No commit or push performed.
## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-04 | Step 1 | Cross-platform entry, four launches, release trigger and documentation implemented and locally validated. |
