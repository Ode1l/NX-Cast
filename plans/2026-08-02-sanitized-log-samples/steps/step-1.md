# Step 1: Generate Sanitized Sample Set

> Status: COMPLETED
> Created: 2026-08-02

## Goal
Create a complete `sample/nxlink-logs/` copy of the current trace corpus with sensitive fields redacted and every file indexed.

## Prerequisites
- The 19 ignored `logs/*.log` inputs have been inventoried and must remain unmodified.
- Files to create: `sample/README.md`, `sample/nxlink-logs/README.md`, and `sample/nxlink-logs/*.log`.
- Design: use the explicitly requested root `sample/` directory and publish all available logs.

## Deliverables
- One sanitized output with the same filename and line count for every current source log.
- An index describing redaction placeholders, profile mapping, test purpose, and observed outcome.
- After this step: the complete sample corpus exists locally and a first-pass leak scan succeeds.

## Plan
- [x] `bash/pwsh` `logs/*.log` → `sample/nxlink-logs/*.log` — apply boundary-aware replacements for URLs, addresses, identifiers, sensitive headers, long tokens, and host paths while preserving event order.
- [x] `write` `sample/README.md` — explain scope and point readers to the nxlink trace index.
- [x] `write` `sample/nxlink-logs/README.md` — document the redaction contract and map all 19 files to Profile IDs/names and outcomes.
- [x] `bash/pwsh` `logs/*.log` and `sample/nxlink-logs/*.log` — compare filenames/line counts and run a first-pass residual pattern scan.

## Quality Checklist

- [x] Evidence-before-edit: target read `logs/*.log` via bounded inventory, impact search `rg sanitize|redact|sample`, validation PowerShell corpus comparison and leak scan.
- [x] Existing pattern / reuse checked: no nxlink publication sanitizer exists under `scripts/` or docs.
- [x] Contract understood: inputs are ignored raw logs; outputs preserve diagnostic context and never expose sensitive values.
- [x] Risk reviewed: correctness / data / security / observability / project-fit.
- [x] Mitigation recorded: complete file/line-count comparison, semantic placeholders, boundary-aware regexes, and independent residual scans.

## Validation Checklist
- [x] Source and output filename sets are identical and both contain 19 `.log` files.
- [x] Per-file source/output line counts match and all Profile markers remain present.
- [x] `git check-ignore sample/nxlink-logs/*.log` reports the outputs are trackable.

## Test Checklist
- [x] Synthetic replacement probes preserve `res=home/home/...`, `sdmc:/...`, and `00:01:23` while redacting URLs, signed scheme-less locations, addresses, IDs, tokens, headers, and host paths.
- [x] Residual scan of all generated outputs reports zero sensitive-pattern matches, including zero parsed IPv6 addresses.

## Implementation Notes
Generated all 19 outputs from the ignored raw directory without changing the sources. The first probe exposed an over-broad IPv6 expression that matched 147 numeric media-time values; regeneration removed that rule because the corpus contains zero parsed IPv6 addresses. Final checks preserved all filenames, line counts, Profile markers, 147 media-time values, 32 Home resource tuples, and 247 `sdmc:/` references. The index covers each output exactly once.

## Files Changed
- `sample/README.md`
- `sample/nxlink-logs/README.md`
- `sample/nxlink-logs/run_nxlink-20260722-015737.log`
- `sample/nxlink-logs/run_nxlink-20260722-015831.log`
- `sample/nxlink-logs/run_nxlink-20260722-015922.log`
- `sample/nxlink-logs/run_nxlink-20260722-020032.log`
- `sample/nxlink-logs/run_nxlink-20260722-020323.log`
- `sample/nxlink-logs/run_nxlink-20260722-020526.log`
- `sample/nxlink-logs/run_nxlink-20260722-020734.log`
- `sample/nxlink-logs/run_nxlink-20260722-021024.log`
- `sample/nxlink-logs/run_nxlink-20260722-021400.log`
- `sample/nxlink-logs/run_nxlink-20260722-021621.log`
- `sample/nxlink-logs/run_nxlink-20260722-022510.log`
- `sample/nxlink-logs/run_nxlink-20260722-023200.log`
- `sample/nxlink-logs/run_nxlink-20260722-024028.log`
- `sample/nxlink-logs/run_nxlink-20260722-124754.log`
- `sample/nxlink-logs/run_nxlink-20260722-132003.log`
- `sample/nxlink-logs/run_nxlink-20260722-133422.log`
- `sample/nxlink-logs/run_nxlink-20260722-193121.log`
- `sample/nxlink-logs/run_nxlink-20260722-212111.log`
- `sample/nxlink-logs/run_nxlink-20260723-002338.log`
- `plans/2026-08-02-sanitized-log-samples/plan.md`
- `plans/2026-08-02-sanitized-log-samples/steps/step-1.md`
