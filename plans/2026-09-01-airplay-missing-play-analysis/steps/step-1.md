# Step 1: Reconstruct AirPlay Control Timeline

> Status: COMPLETED

## Goal
Explain later AirPlay attempts and the visible error from ordered log evidence.

## Prerequisites
- Latest full trace is available.

## Deliverables
- Ordered request/session/owner/error timeline.
- Root-cause report with verified and unknown findings.

## Plan
1. Extract all AirPlay media entry points and session boundaries.
2. Correlate player and coordinator transitions.
3. Inspect every error with surrounding context.

## Quality Checklist
- [x] No sender-specific assumption is presented as fact.
- [x] Distinct protocol paths remain distinct.

## Validation Checklist
- [x] All `/play`, RTSP `SETUP`, `RECORD`, `TEARDOWN`, owner claims, and player errors are covered.

## Test Checklist
- [x] Timeline is internally ordered by log line and monotonic timestamp.

## Implementation Notes
- Analysis only; no source changes planned.
- Three HTTP `/play` requests occur at lines 190, 675, and 2391.
- Later sessions 18 and 19 are audio-only RAOP attempts that the sender tears down without video negotiation.
- The home-screen error is the retained IPTV failure from lines 2159-2166, not an AirPlay failure.

## Files Changed
- `plans/2026-09-01-airplay-missing-play-analysis/plan.md`
- `plans/2026-09-01-airplay-missing-play-analysis/steps/step-1.md`
