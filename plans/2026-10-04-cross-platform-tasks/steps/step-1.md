# Step 1: Cross-platform workflows

> Status: COMPLETED
> Created: 2026-10-04

## Goal
Deliver shared build/upload/publish commands with small platform adapters and four launch entries.
## Prerequisites
- User approved the workflow and cross-platform approach.
- Existing scripts and release workflow inspected.
## Deliverables
- Shared SDK initialization, command entry and safe GitHub release trigger.
- Windows adapter, simplified task/launch configuration, usage notes and isolated tests.
## Plan
- [x] Implement shared scripts and MSYS-compatible dependency installation.
- [x] Replace VS Code entries, document environment and release behavior.
- [x] Run local build and isolated regression tests without network publication.
## Quality Checklist
- [x] Evidence: current scripts/config and callers searched.
- [x] Reuse: Make, nxlink and tag release workflow, no duplicated build logic.
- [x] Contract: no implicit commit, no overwritten tags, normal Trace disabled.
- [x] Risks: accidental publication, platform paths, dirty working tree.
- [x] Mitigation: fake SDK/uploader, local bare Git fixtures; no real publish invocation.
## Validation Checklist
- [x] Shell/JSON validation and diff check pass.
- [x] Windows execution limitations explicitly recorded.
## Test Checklist
- [x] Isolated script tests and FFmpeg installer tests pass.
- [x] Shared build entry succeeds on macOS.
## Implementation Notes
Implemented shared dev.sh actions and SDK initialization, a PowerShell adapter selecting MSYS2 explicitly, four launches and atomic Git release publication. Existing task names were consolidated; two composite tasks sequence clean/build/upload. Release does not need the SDK, auto-commit, overwrite tags or upload a local binary. It validates main/clean tree/version/notes and pushes main plus tag atomically. CRLF metadata is accepted. Windows dependency installation selects pacman without sudo; other platforms retain the previous behavior.

Validation: 9 workflow tests passed using fake SDK/nxlink and local bare Git repositories (including rejected-main atomicity, old tags, dirty tree, branch, notes, CRLF and path normalization). 11 installer tests passed, including simulated MSYS. bash -n, JSON reference checks and git diff --check passed. bash scripts/dev.sh build succeeded on macOS, reused FFmpeg 7.1-4, emitted a normal Trace-off NRO and no warning/error matches in /tmp/nxcast-cross-platform-build.log. No real upload or publication was performed.

Limitations: PowerShell/Windows host unavailable, so native Windows execution and real Switch nxlink upload remain unverified. Existing devkitPro Make rules require workspace/SDK paths without spaces; documented rather than silently adding a fragile 8.3-path workaround. No protocol/coordinator/player or GitHub workflow edits. Earlier IPTV/build review fixes preserved uncommitted.
## Files Changed
- .vscode/tasks.json
- .vscode/launch.json
- .gitattributes
- scripts/dev.sh
- scripts/dev.ps1
- scripts/dev_environment.sh
- scripts/publish_release.sh
- scripts/run_nxlink.sh
- scripts/install_switch_ffmpeg_airplay.sh
- scripts/test_dev_workflow.py
- scripts/test_ffmpeg_install.py
- docs/developer-workflow.md
- docs/LOCAL_VSCODE_DIAGNOSTIC_WORKFLOW.md
- docs/AIRPLAY_DEVELOPMENT.md
- README.md
- README_CN.md
- plans/2026-10-04-cross-platform-tasks/plan.md
- plans/2026-10-04-cross-platform-tasks/steps/step-1.md
