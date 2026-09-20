# Step 3: Publish GitHub Release

> Status: PENDING
> Created: 2026-09-19

## Goal
Publish the validated source and assets through the repository's tagged-release workflow.

## Prerequisites
- Step 2 completed with all local release gates passing.
- `v0.3.0` remains absent locally and remotely.
- GitHub CLI and Git push authentication remain valid.

## Deliverables
- Release metadata commit on `airplay`.
- `main` fast-forwarded to the validated release commit and pushed.
- Annotated `v0.3.0` tag pushed.
- Successful GitHub Release with `NX-Cast.nro` and `NX-Cast-sdmc.zip` assets.
- After this step: users can download and install NX-Cast v0.3.0.

## Plan
- [ ] `bash` stage only release metadata and inspect staged diff/secrets.
- [ ] `bash` commit and push `airplay`.
- [ ] `bash` fast-forward `main`, push it, create annotated `v0.3.0`, and push the tag.
- [ ] `bash` watch tagged workflow to completion and inspect GitHub Release assets.
- [ ] `bash` compare release/tag commit and asset presence; report URLs and hashes.

## Quality Checklist
- [ ] Evidence-before-edit: branch ancestry, tag absence, workflow and auth verified.
- [ ] Existing pattern / reuse checked: repository tagged-release workflow used unchanged.
- [ ] Contract understood: tag push creates a public immutable release entry and assets.
- [ ] Risk reviewed: wrong commit/tag, CI failure, incomplete assets, unrelated local files.
- [ ] Mitigation recorded: pre-push SHA checks, fast-forward-only merge, workflow wait, release inspection.

## Validation Checklist
- [ ] `git status`, `git show v0.3.0`, and remote refs identify the intended release commit.
- [ ] GitHub tagged workflow completes successfully.
- [ ] GitHub Release lists both required assets.

## Test Checklist
- [ ] Remote CI repeats host tests and strict Switch build successfully.

## Implementation Notes
Pending.

## Files Changed
Pending.

