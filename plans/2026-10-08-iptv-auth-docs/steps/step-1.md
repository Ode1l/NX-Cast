# Step 1: Document Authentication Limits

> Status: COMPLETED
> Created: 2026-10-08

## Goal
Add a concise authentication section to the existing IPTV guide.

## Prerequisites
- User requested documentation, not new authentication functionality.
- IPTV guide and relevant code inspected.

## Deliverables
- Supported URL-token example and explicit unsupported authentication behavior.

## Plan
- [x] Edit docs/iptv.md.
- [x] Review the diff and whitespace.

## Quality Checklist
- [x] Evidence-before-edit: guide and IPTV URL/fetch/parser code inspected.
- [x] Existing pattern / reuse checked: existing SD data privacy paragraph.
- [x] Contract understood: document, do not change playback.
- [x] Risk reviewed: exposing tokens or promising automatic authentication.
- [x] Mitigation recorded: dummy token example and plaintext-storage warning.

## Validation Checklist
- [x] Diff matches current implementation.
- [x] git diff --check passes.

## Test Checklist
- N/A: documentation-only change.

## Implementation Notes
Documented URL tokens without promising credential refresh, custom headers or query propagation. Added URL capacity and plaintext SD data warning. No runtime changes or publication; protocol selection remains a discussion.

## Files Changed
- docs/iptv.md
- plans/2026-10-08-iptv-auth-docs/plan.md
- plans/2026-10-08-iptv-auth-docs/steps/step-1.md
