# Step 2: Validate and Document the Published Evidence

> Status: COMPLETED
> Created: 2026-08-02

## Goal
Independently verify the sample corpus and connect it to the existing diagnostics and macOS handoff documentation.

## Prerequisites
- Step 1 completed with all 19 sanitized logs and the sample index present.
- Files to modify: `docs/README.md`, `docs/MACOS_HANDOFF_2026-07-23.md`, and Step 1 outputs if validation finds a defect.

## Deliverables
- Documentation links that make the sample corpus discoverable without claiming the raw logs are public.
- Independent fidelity and leakage evidence recorded in the plan.
- After this step: documentation and samples are internally consistent and ready to stage.

## Plan
- [x] `edit` `docs/README.md` — add the sanitized nxlink sample index to recommended diagnostics reading.
- [x] `edit` `docs/MACOS_HANDOFF_2026-07-23.md` — replace the blanket raw-log omission statement with a link to sanitized samples while retaining the raw-secret exclusion.
- [x] `bash/pwsh` `sample/nxlink-logs/*.log` — run independent residual scans and profile/final-state coverage checks.
- [x] `bash` repository diff — run `git diff --check` and review bounded `git diff --stat`/`git status` output.

## Quality Checklist

- [x] Evidence-before-edit: target read `docs/README.md` and `docs/MACOS_HANDOFF_2026-07-23.md`, impact search for existing sample references, validation commands defined in `plan.md`.
- [x] Existing pattern / reuse checked: use existing relative Markdown link conventions in `docs/README.md`.
- [x] Contract understood: documentation may link sanitized traces but must not imply raw signed URLs or device material are committed.
- [x] Risk reviewed: correctness / data / security / observability.
- [x] Mitigation recorded: independent second-pass scans, table coverage check, and explicit privacy wording.

## Validation Checklist
- [x] Every table row references an existing sample file and every sample file appears exactly once in the index.
- [x] Profile IDs/names agree with retained startup markers and the handoff results table.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] Privacy scan exits 0 with zero raw URLs, LAN addresses, identity tokens, sensitive headers, or host paths.
- [x] Documentation-link target checks confirm all relative sample links resolve.

## Implementation Notes
Linked the complete sample index from the documentation landing page and updated the macOS handoff to distinguish excluded raw captures from the new sanitized copies. An independent 12-pattern scan passed across all 19 logs. The index maps 16 Profile-bearing logs and three pre-test captures one-to-one, and all documentation/sample links resolve. Runtime build/tests were not run because this step changes only historical evidence and Markdown.

## Files Changed
- `docs/README.md`
- `docs/MACOS_HANDOFF_2026-07-23.md`
- `plans/2026-08-02-sanitized-log-samples/plan.md`
- `plans/2026-08-02-sanitized-log-samples/steps/step-2.md`
