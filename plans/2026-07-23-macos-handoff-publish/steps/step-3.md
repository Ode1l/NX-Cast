# Step 3: Commit and Push the Curated Handoff

> Status: IN_PROGRESS
> Created: 2026-07-23

## Goal
Commit only the approved semantic manifest, push it to `origin/airplay`, and prove the remote head matches the final local commit.

## Prerequisites
- Steps 1 and 2 completed with passing required validation and a reviewed candidate manifest.
- Connected GitHub app is available; `git ls-remote` resolves `origin/airplay`.
- Exclusions: `.vscode/tasks.json`, `.vscode/launch.json`, `plans/2026-07-22-vscode-space-path-build/`, generated/ignored paths, and `NUL`.

## Deliverables
- One intentional implementation/handoff commit plus, if needed, one final workflow-record commit.
- Updated `origin/airplay` whose SHA matches local `airplay`.
- Local Windows VS Code files remain modified and unstaged.
- After this step: the macOS workspace can pull `airplay` and continue from the documented state.

## Plan
- [ ] `git add` explicit approved paths — never use blanket staging on the mixed worktree.
- [ ] `git diff --cached` review names, stats, whitespace, artifact patterns, and `.vscode` exclusion.
- [ ] `git commit` with a terse resource-management/diagnostics handoff message.
- [ ] `git push -u origin airplay` — publish the requested branch without opening a pull request.
- [ ] `git/connector` verify remote SHA and repository accessibility; update the workflow record and push its final documentation commit if required.

## Quality Checklist
- [ ] Evidence-before-edit: branch/remote/status/diff inspected; staged manifest validated before commit.
- [ ] Existing pattern / reuse checked: stay on the existing tracked feature branch.
- [ ] Contract understood: push is authorized; PR creation, binaries, logs, and Windows tasks are not.
- [ ] Risk reviewed: accidental broad staging, credential failure, remote divergence.
- [ ] Mitigation recorded: explicit path list, remote-head precheck, staged exclusion assertions, post-push SHA comparison.

## Validation Checklist
- [ ] `git diff --cached --check` passes before each commit.
- [ ] `git ls-remote --heads origin airplay` equals `git rev-parse HEAD` after the final push.

## Test Checklist
- [ ] No tests rerun after documentation-only workflow-status update; Step 2 evidence remains applicable.

## Implementation Notes
Pending.

## Files Changed
Pending.
