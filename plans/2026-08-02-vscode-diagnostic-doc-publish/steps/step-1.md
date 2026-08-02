# Step 1: Document the Diagnostic Launch Workflow

> Status: COMPLETED
> Created: 2026-08-02

## Goal
Create a portable documentation record of the local VS Code launch/task graph without publishing machine-specific JSON or Make commands.

## Prerequisites
- Requirements are confirmed and `Open Questions` is `None.`.
- Source records read: `.vscode/launch.json`, `.vscode/tasks.json`, July handoff, docs index, and local Windows path plan.
- Files to modify: `docs/LOCAL_VSCODE_DIAGNOSTIC_WORKFLOW.md`, `docs/MACOS_HANDOFF_2026-07-23.md`, `docs/README.md`, and `plans/context.md`.

## Deliverables
- A standalone document records launch/task labels, profile picker, runtime flow, platform placeholders, exclusions, and troubleshooting boundaries.
- After this step: a developer on another OS can reconstruct the workflow locally without copying Windows-specific paths or changing committed build configuration.

## Plan
- [x] `write` `docs/LOCAL_VSCODE_DIAGNOSTIC_WORKFLOW.md` — record the stable launch/task/profile contract and OS-specific substitutions.
- [x] `edit` `docs/MACOS_HANDOFF_2026-07-23.md` — link the new workflow as the authoritative VS Code reconstruction record.
- [x] `edit` `docs/README.md` — add the workflow to recommended/build documentation.
- [x] `edit` `plans/context.md` — record publication scope, exclusions, and the next macOS action.
- [x] `read/rg` the four documents — verify labels/profile names, links, redaction, and absence of hardcoded drive-specific executable paths.

## Quality Checklist
- [x] Evidence-before-edit: targets read, impact search limited to documentation links, validation commands identified.
- [x] Existing pattern / reuse checked: extended the existing handoff and documentation index rather than duplicating the test matrix.
- [x] Contract understood: documentation describes local reconstruction; no configuration or build behavior changes.
- [x] Risk reviewed: stale label names, accidental platform-specific commands, duplicated test claims.
- [x] Mitigation recorded: exact labels came from parsed JSON; test results remain linked to the July handoff.

## Validation Checklist
- [x] Documentation links resolve locally and `git diff --check` passes for all four documents.
- [x] `rg` confirms exact launch/task/default-profile labels and no Windows drive-prefixed Bash executable in the new document.

## Test Checklist
- [x] Local `.vscode/tasks.json` and `.vscode/launch.json` still parse; no runtime tests are required for documentation-only changes.

## Implementation Notes
Added a standalone portable workflow record with the exact launch and task
labels, all Profiles 1-14, the Profile 14 default, the stable nxlink execution
contract, Windows space-path findings, platform substitution rules, exclusions,
and a reconstruction checklist. Linked it from both the July handoff and the
documentation index, and updated session context without duplicating the test
matrix. The local `.vscode` changes were protected in a named stash before the
documentation edit, restored byte-for-byte afterward, and the temporary stash
was dropped. JSON parsing, launch-to-task reference, picker count/default, local
links, redaction, full re-read, scoped diff review, and whitespace checks passed.
Impact search was documentation-only; no runtime/API contract changed and no
build test was warranted.

## Files Changed
- `docs/LOCAL_VSCODE_DIAGNOSTIC_WORKFLOW.md` — created portable workflow record.
- `docs/MACOS_HANDOFF_2026-07-23.md` — linked authoritative workflow record.
- `docs/README.md` — indexed the new document.
- `plans/context.md` — updated current publication scope and macOS next action.
