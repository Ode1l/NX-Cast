# Step 1: Prepare Release Metadata

> Status: COMPLETED
> Created: 2026-09-19

## Goal
Protect the tested implementation and prepare accurate v0.3.0 version metadata and release notes.

## Prerequisites
- User approved release version `0.3.0`.
- Current product changes and untracked source files were inventoried.
- Existing tag/release conventions were read.

## Deliverables
- A pre-release feature commit containing only product source, tests, and documentation.
- `makefile`, `README.md`, `CHANGELOG.md`, and `.github/release-notes.md` describe v0.3.0.
- After this step: the remaining diff contains only release metadata.

## Plan
- [x] `bash` selective `git add` and staged review — protect current tested product work without plans/logs.
- [x] `bash` `git commit` — create the pre-release feature commit.
- [x] `edit` `makefile` — set `APP_VERSION` to `0.3.0`.
- [x] `edit` `CHANGELOG.md` and `.github/release-notes.md` — record tested v0.3.0 features and limitations.
- [x] `edit` `README.md` — update the formal release command example to v0.3.0.
- [x] `bash` `git diff --check` and version search — verify consistent metadata.

## Quality Checklist
- [x] Evidence-before-edit: release files read, version usage searched, validation commands identified.
- [x] Existing pattern / reuse checked: v0.2.0 changelog and tagged workflow reused.
- [x] Contract understood: version enters NACP; release notes become public GitHub body.
- [x] Risk reviewed: accidental staging of secrets/logs/plans and overstated AirPlay support.
- [x] Mitigation recorded: explicit path staging and experimental limitations retained.

## Validation Checklist
- [x] `git diff --check` exits 0.
- [x] `rg` finds `0.3.0` in authoritative version/release files and no stale current version in `makefile`.

## Test Checklist
- [x] N/A for metadata; strict build and host suite are Step 2 gates.

## Implementation Notes
The pre-release implementation was committed as `557c92e` after a staged whitespace and sensitive-content scan. Metadata now consistently names v0.3.0 while preserving explicit experimental AirPlay limitations. `README.md` was included as a direct source-of-truth dependency because its formal tag example still named v0.2.0.

## Files Changed
- `makefile`
- `README.md`
- `CHANGELOG.md`
- `.github/release-notes.md`
