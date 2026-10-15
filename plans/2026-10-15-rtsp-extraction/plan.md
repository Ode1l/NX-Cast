# Plan: Incremental RTSP Request Parser Extraction

> Status: COMPLETED
> Created: 2026-10-15
> Last Updated: 2026-10-15

## Goal
Extract a reusable request parser while preserving the existing AirPlay behavior and produce a Switch build for device regression.

## Assumptions
- Device playback verification is performed by the user after local validation.
- Existing root NRO/ELF files are prior artifacts, not proven builds of current HEAD.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- Existing AirPlay request APIs keep their types, limits, return values and ownership rules.
- The common parser has no AirPlay session, security or player dependency.
- Existing RTSP and related session tests pass; a Switch NRO and matching ELF build successfully.
### Non-goals
- Miracast implementation, media bridge extraction, response codec extraction, timeout/cache changes and release publication.
### Edge Cases
- Fragmented and concatenated requests, binary bodies, duplicate length/CSeq headers, oversized messages, cleanup on errors.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| First slice | Entire protocol rewrite / independent parser extraction | Common request parser with compatibility wrappers; retain AirPlay dispatch and responses | User approved incremental refactoring; scope explained in commentary |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Extract and locally validate the parser; hand off device checks |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Baseline | Host cc with existing rtsp.c and scripts/test_airplay_rtsp.c | Existing make test-airplay recipe | yes |
| Regression | make test-airplay | makefile | yes, subject to host dependencies |
| Build | bash scripts/dev.sh build | scripts/dev.sh | yes |
| Diff | git diff --check | Git | yes |

## Context & Learnings
### Key Decisions
- Preserve parser algorithms and AirPlay-facing symbols; move generic helpers without changing their logic.
### Gotchas & Warnings
- Response objects carry AirPlay reverse-connection metadata; leave that boundary for a later independently tested slice.
- devkitPro uses basename object files, so the common source must not also be called rtsp.c.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| source/protocol/airplay/protocol/rtsp.c | Existing parser, encoder and dispatcher | Read function boundaries and parser implementation |
| source/protocol/airplay/protocol/rtsp.h | Existing public contract | Read complete header |
| scripts/test_airplay_rtsp.c | Existing regression tests | Read fragmentation tests and test inventory |
| makefile | Switch source directories and host link recipes | rg rtsp.c and SOURCES |
### Verified Facts
- Starting clean main HEAD: 0efdba4281122cdd97de0c3c1a7ebde7485d6d15.
- Request parsing already supports RTSP/1.0 and HTTP/1.1 and owns a separately allocated body.
- Host test recipes explicitly list rtsp.c; all linking recipes need the new parser source.
- Existing RTSP tests pass before extraction and after extraction with ASan/UBSan.
- Entire make test-airplay suite passed, including persistent RTSP, pairing, mDNS and composed receiver smoke tests.
- Parser bodies match baseline after normalizing symbol names and whitespace; response/session code matches verbatim.
- Normal Switch build passed using pinned FFmpeg 7.1-4. Device playback remains unverified.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-15 | 1 | Scope restricted to the independently testable request parser. |
| 2026-10-15 | 1 | Local validation completed; device checklist and artifact hashes recorded in step-1.md. No commit, push or release performed. |
