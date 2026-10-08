# Plan: IPTV Authentication Boundaries

> Status: COMPLETED
> Created: 2026-10-08
> Last Updated: 2026-10-08

## Goal
Document existing IPTV authentication limits without adding authentication features; discuss candidate casting protocols separately.

## Assumptions
- The user clarified that the intended protocol is Miracast.

## Open Questions
None.

## Spec-Lite
N/A - covered by Goal, Deliverables, and Validation.

### Acceptance Criteria
- [x] URL tokens, header limits, expiration and storage privacy are explicit.

### Non-goals
- Protocol implementation, automatic login or token refresh, commits and publication.

### Edge Cases
- A playlist token is not automatically inherited by its channel URLs.

## Design Decisions
None - documentation of existing behavior only.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Document authentication limits and validate against code. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Whitespace | `git diff --check` | Git | yes |
| Review | `git diff -- docs/iptv.md` | Documentation scope | yes |
| Runtime tests | N/A | No behavior changes | no |

## Context & Learnings
### Key Decisions
- Reuse docs/iptv.md; keep next-protocol discussion in the response.
### Gotchas & Warnings
- Do not imply all signed URLs work or that saved credentials are encrypted.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| docs/iptv.md | User guide | Read 2026-10-08 |
| source/iptv/url.c, source/iptv/iptv.c, source/iptv/iptv.h, source/iptv/fetch.c | Behavior evidence | Previous focused code inspection |
### Verified Facts
- URL capacity is 1024 including terminator; absolute references are copied unchanged and non-EXTINF comment directives are skipped.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-08 | 1 | Added IPTV URL/authentication boundaries and privacy warning; whitespace and scoped diff passed. Consulted Microsoft, Google/Open Screen, FCast, DIAL and Moonlight primary sources for protocol discussion. |
