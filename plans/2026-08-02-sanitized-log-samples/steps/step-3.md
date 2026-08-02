# Step 3: Commit and Push Sanitized Samples

> Status: IN_PROGRESS
> Created: 2026-08-02

## Goal
Publish only the approved sample, documentation, and current workflow-plan files to the remote `airplay` branch.

## Prerequisites
- Steps 1 and 2 completed with all validation checks passing.
- The local `.vscode` changes and `plans/2026-07-22-vscode-space-path-build/` remain unstaged.
- Files eligible to stage: `sample/`, the two intended documentation files, and `plans/2026-08-02-sanitized-log-samples/`.

## Deliverables
- One intentional commit containing the sanitized sample corpus and its documentation.
- A successful push to `origin/airplay` with local and remote commit IDs verified equal.
- After this step: GitHub contains the sample folder without environment-specific configuration, raw logs, or build artifacts.

## Plan
- [ ] `bash` explicit pathspecs — stage only `sample/`, `docs/README.md`, `docs/MACOS_HANDOFF_2026-07-23.md`, and `plans/2026-08-02-sanitized-log-samples/`.
- [ ] `bash` `git diff --cached --name-only` and `git diff --cached --check` — verify exclusions and patch hygiene before commit.
- [ ] `bash` `git commit` — create an intentional documentation/sample commit.
- [ ] `bash` `git push origin airplay` and remote refs — push and confirm local/tracking/remote SHA equality.

## Quality Checklist

- [ ] Evidence-before-edit: no new content edit; staging scope inspected with `git status` and cached diff commands.
- [ ] Existing pattern / reuse checked: current branch/remote workflow matches the two preceding documentation pushes.
- [ ] Contract understood: pushing is authorized by the user's explicit request; excluded local files must remain outside the commit.
- [ ] Risk reviewed: data / security / project-fit.
- [ ] Mitigation recorded: explicit path staging, cached leak scan, cached file-list review, and remote SHA verification.

## Validation Checklist
- [ ] Cached file list contains no `.vscode`, `makefile`, `.github/workflows`, raw `logs/`, compiled output, or older local plan files.
- [ ] `git diff --cached --check` exits 0 and the commit succeeds.
- [ ] `git rev-parse HEAD`, `git rev-parse @{u}`, and `git ls-remote origin refs/heads/airplay` report the same SHA after push.

## Test Checklist
- [ ] Re-run the residual privacy scan against the staged sample files before commit.
- [ ] Post-push `git status --short --branch` shows only the pre-existing local-only changes.

## Implementation Notes
Pending.

## Files Changed
Pending.
