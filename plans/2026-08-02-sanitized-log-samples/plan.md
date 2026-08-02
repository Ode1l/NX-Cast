# Plan: Publish Sanitized nxlink Log Samples

> Status: ACTIVE
> Created: 2026-08-02
> Last Updated: 2026-08-02

<!--
  Plan-level status (lifecycle):
    DRAFT     — awaiting approval after clarification
    ACTIVE    — execution in progress
    COMPLETED — all steps done, verified
    ARCHIVED  — optional long-term archival state
  This is distinct from step-level status (PENDING|IN_PROGRESS|COMPLETED|BLOCKED)
  in `steps/step-N.md`. The pre-edit gate checks step status, not plan status.
-->

## Goal
Publish every available nxlink test log under a dedicated `sample/` directory in a reviewable, privacy-safe form with an index that connects each trace to its diagnostic profile and observed result.

## Assumptions
- The requested upload covers all 19 files currently present in the ignored `logs/` directory, including short failed-transfer traces.
- Raw signed media URLs, LAN addresses, device/pairing identifiers, authentication material, and machine-specific absolute paths must not be committed.
- Existing local `.vscode` changes and the older untracked Windows path plan remain local and outside this commit.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [ ] `sample/nxlink-logs/` contains one sanitized file for every current `logs/*.log` input, with identical filenames and line counts.
- [ ] Samples retain timestamps, diagnostic profile markers, state transitions, error codes, counters, and shutdown outcomes needed for debugging.
- [ ] Automated scans find no raw HTTP(S) URL, IPv4/MAC/UUID, long cryptographic-looking token, sensitive header value, or personal/toolchain absolute path in the committed samples.
- [ ] A Markdown index explains the sanitization contract and maps all samples to profiles/results.
- [ ] Only intended sample, documentation, and workflow-plan files are committed and the `airplay` branch is pushed successfully.

### Non-goals
- Commit raw `logs/`, build outputs, `.vscode/tasks.json`, `.vscode/launch.json`, Makefile changes, or CI workflow changes.
- Change runtime behavior, diagnostic formatting, player/network code, or historical test conclusions.

### Edge Cases
- Tiny logs that contain only nxlink ping/upload failures are retained because they document failed collection attempts.
- Scheme-less media locations in `url=`, `uri=`, `location=`, and SOAP `CurrentURI` fields are redacted in addition to normal HTTP(S) URLs.
- Redaction must not mistake compact state tuples such as `res=home/home/...` or `sdmc:/...` paths for host filesystem paths.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Sample location | `samples/`, `docs/logs/`, root `sample/` | Root `sample/nxlink-logs/` — follows the user's explicit folder request and separates traces from prose docs | yes — explicit user request |
| Coverage | A few representative traces vs every available trace | Publish all 19 sanitized traces, including failed captures | yes — user reported many missing logs and requested an upload set |
| Privacy model | Raw logs vs manual excerpts vs deterministic field redaction | Full sanitized copies with documented placeholders and independent residual scans | yes — required to avoid publishing signed URLs and local identity data |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Generate the complete sanitized sample set and its local index. |
| Step 2 | `steps/step-2.md` | COMPLETED | Independently validate fidelity/privacy and link the sample set from durable diagnostics docs. |
| Step 3 | `steps/step-3.md` | IN_PROGRESS | Stage only approved files, commit, push, and verify the remote branch. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Inventory/fidelity | `pwsh -NoProfile -Command '<compare logs and sample/nxlink-logs names and line counts>'` | Task acceptance criteria | yes |
| Privacy scan | `pwsh -NoProfile -Command '<scan sample/nxlink-logs for URL, address, identifier, token, header, and path patterns>'` | Data/security risk review | yes |
| Ignore/staging check | `git check-ignore -v logs/*; git status --short; git diff --cached --name-only` | `.gitignore` and user exclusion request | yes |
| Markdown/diff hygiene | `git diff --check` | Git built-in | yes |
| Runtime build | Not run — only historical sample logs, Markdown, and workflow records change | Scope inspection | no |

## Context & Learnings
### Key Decisions
- Preserve full event order and counters while replacing sensitive values with semantic placeholders such as `<URL_REDACTED>` and `<IP_REDACTED>`.
- Keep original filenames so the handoff result table and historical timestamps remain easy to correlate.
### Gotchas & Warnings
- A simple `https://` replacement is insufficient: several DLNA/IPTV records contain signed or host/path media values without a scheme.
- Broad `/home/` or drive-path regular expressions can corrupt valid state tuples or `sdmc:/` paths; host-path matching must be boundary-anchored.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/*.log` | Ignored source captures to sanitize | PowerShell inventory found 19 files totaling 2,359,627 bytes; profile-marker scan covered IDs 1–14 |
| `.gitignore` | Defines raw-log/build exclusions | `read .gitignore` confirms `logs/`, `build/`, `sdmc/`, artifacts, and compiled Switch outputs are ignored |
| `docs/MACOS_HANDOFF_2026-07-23.md` | Durable mapping from profiles to observed results | `read` confirms results for Profiles 1–14 and intentionally omitted raw logs |
| `docs/README.md` | Documentation discovery index | `read` confirms diagnostics/handoff links but no sample-log link |
| `scripts/` | Reuse search for an existing sanitizer | `rg sanitize|redact` and script inventory found test sanitizers but no nxlink-log publication sanitizer |
| `sample/nxlink-logs/*.log` | Sanitized publication corpus | 19-file name/line-count comparison, Profile-marker preservation, semantic-state preservation, and residual privacy scan passed in Step 1 |
| `sample/nxlink-logs/README.md` | Redaction contract and capture index | Index-to-file comparison found exactly 19 one-to-one entries in Step 1 |
| `docs/README.md` | Documentation discovery index | Added and resolved the sanitized sample link; diff reviewed in Step 2 |
| `docs/MACOS_HANDOFF_2026-07-23.md` | Durable test interpretation | Updated raw/sanitized evidence wording and resolved the sample link in Step 2 |

### Verified Facts
- The branch is `airplay` and tracks `origin/airplay` with only local `.vscode` changes plus `plans/2026-07-22-vscode-space-path-build/` before this task — verified by `git status --short --branch`, 2026-08-02.
- The raw-log directory is already ignored, while `sample/` is not covered by a global `*.log` ignore — verified by `.gitignore`, 2026-08-02.
- Current captures include normal URLs, scheme-less DLNA/IPTV media locations, LAN IPv4 addresses, long identity-like values, and local absolute paths — verified by bounded regex inventory of `logs/*.log`, 2026-08-02.
- No reusable nxlink-log sanitizer or existing sample-log directory was found — verified by targeted `rg` and `scripts/` inventory, 2026-08-02.
- All 19 source/output filenames and line counts match; Profile markers, 147 media-time values, 32 Home tuples, and 247 `sdmc:/` references were preserved — verified by PowerShell corpus comparison, Step 1.
- The final sanitized corpus has zero residual raw URL/address/identifier/header/host-path findings and zero tokens parsed as IPv6 addresses — verified by regex and `System.Net.IPAddress` scans, Step 1.
- The sample index maps all 19 files exactly once (16 diagnostic Profiles plus three failed collection attempts), all links resolve, and an independent 12-pattern privacy scan passes — verified by PowerShell checks, Step 2.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-02 | Step 1 | Generated and indexed 19 sanitized nxlink traces; corrected a probe-detected IPv6/media-time false positive and passed fidelity/privacy scans. |
| 2026-08-02 | Step 2 | Linked sanitized evidence from the docs/handoff and passed index, profile, privacy, link, whitespace, and diff checks. |
