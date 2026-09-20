# Step 1: Correlated Negotiation Summary

> Status: COMPLETED
> Created: 2026-09-09

## Goal
Add a compact per-connection AirPlay negotiation summary that identifies the first missing video boundary.

## Prerequisites
- Existing handler and remote-video routes inspected.
- Diagnostic format confirmed as protocol-generic and secret-free.

## Deliverables
- Stage accounting in `handlers.c` with connection and logical-session correlation.
- Focused tests verify summary state does not alter protocol responses or lifecycle state.

## Plan
- [x] `edit` `source/protocol/airplay/protocol/handlers.c` — record successful and failed negotiation boundaries and emit a close summary.
- [x] `test` existing handler transcripts — representative mirror, remote `/play`, and negotiation-stop paths emit summaries without changing outcomes.
- [x] `bash` `make test-airplay` — all host tests pass.

## Quality Checklist
- [x] Evidence-before-edit: handler/remote runtime read, route impact searched, `make test-airplay` identified.
- [x] Existing pattern / reuse checked: existing handler context and `AIRPLAY_TRACE_SYNC` will be reused.
- [x] Contract understood: diagnostics must not affect response status, callbacks, ownership, or lifecycle.
- [x] Risk reviewed: accidental logging of secrets and accidental protocol-state coupling.
- [x] Mitigation recorded: log only booleans/counts/status names and keep diagnostics write-only.

## Validation Checklist
- [x] `make test-airplay` exits 0.
- [ ] `git diff --check` exits 0 in final verification.

## Test Checklist
- [x] Existing AirPlay handler and integration tests remain green.
- [x] Existing traced transcripts cover stage classification without sender-specific assumptions.

## Implementation Notes
Added sticky, diagnostic-only stage flags to the existing handler context. Full Trace now emits a correlated close summary and an immediate `/play` acceptance result. No diagnostic flag participates in protocol decisions. A non-Trace `-Werror` check caught and verified the conditional compilation path.

## Files Changed
- `source/protocol/airplay/protocol/handlers.c`
