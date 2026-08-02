# Step 2: Publish Documentation Only

> Status: IN_PROGRESS
> Created: 2026-08-02

## Goal
Commit and push only the approved Markdown records while proving that environment-specific configuration and generated artifacts remain local.

## Prerequisites
- Step 1 completed and documentation validation passed.
- Current branch remains `airplay`; the remote ref has not diverged.
- Explicit exclusions: `makefile`, `.github/workflows/*`, `.vscode/tasks.json`, `.vscode/launch.json`, `plans/2026-07-22-vscode-space-path-build/`, generated binaries/logs.

## Deliverables
- One documentation commit plus an optional final workflow-status commit on `origin/airplay`.
- After this step: remote documentation contains the launch reconstruction record, while all excluded local changes remain in the worktree.

## Plan
- [ ] `git add` explicit documentation and current-plan paths — never blanket-stage the mixed worktree.
- [ ] `git diff --cached` inspect paths, stats, whitespace, secrets, and forbidden patterns.
- [ ] `git commit` create a terse documentation commit.
- [ ] `git push` publish the current `airplay` branch using authenticated SSH without creating a PR.
- [ ] `git/connector` fetch the remote document and compare local/tracking/remote SHAs; persist final workflow evidence if needed.

## Quality Checklist
- [ ] Evidence-before-edit: status, branch, remote, semantic diff, and exact exclusion list reviewed.
- [ ] Existing pattern / reuse checked: remain on the existing feature branch and use explicit staging.
- [ ] Contract understood: only Markdown records are authorized for publication.
- [ ] Risk reviewed: broad staging, remote divergence, credentials, environment-specific leakage.
- [ ] Mitigation recorded: staged-manifest assertions, pre/post remote SHA checks, SSH push.

## Validation Checklist
- [ ] `git diff --cached --check` passes and forbidden staged paths are absent.
- [ ] Final `git rev-parse HEAD`, `origin/airplay`, and `git ls-remote` SHAs match.

## Test Checklist
- [ ] No build/test rerun after documentation-only commit; Step 1 syntax/link validation remains applicable.

## Implementation Notes
Pending.

## Files Changed
Pending.
