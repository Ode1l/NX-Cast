# Step 1: Correlate AirPlay Sessions

> Status: IN_PROGRESS
> Created: 2026-09-02

## Goal
Produce an evidence-backed report for the three AirPlay trials and the missing title.

## Prerequisites
- Latest log identified as `logs/run_nxlink-20260902-213415.log`.
- Files to inspect: AirPlay remote-video, logical-session, integration, and player metadata paths.

## Deliverables
- A per-trial timeline with the earliest divergence.
- After this step: protocol defects are separated from expected sender behavior and media decode behavior.

## Plan
- [ ] `rg` latest log — enumerate pair/setup/play/action/stop/close and ownership events.
- [ ] `sed` bounded session ranges — correlate sender requests with player state.
- [ ] `rg` AirPlay/player source — identify title extraction and detach/stop contracts.
- [ ] `edit` plan files — persist verified conclusions and residual unknowns.

## Quality Checklist
- [ ] Evidence-before-edit: latest log and direct source paths read; no production edit planned.
- [ ] Existing pattern / reuse checked: current trace markers and logical session identifiers used.
- [ ] Contract understood: control connection, logical session, and media ownership are distinct.
- [ ] Risk reviewed: false attribution from close timing or mixed RTSP/HTTP sessions.
- [ ] Mitigation recorded: report the first causal event, not the last visible event.

## Validation Checklist
- [ ] Every conclusion cites a concrete log event or source contract.
- [ ] The three trials are distinguishable by logical session and media sequence.

## Test Checklist
- [ ] N/A — read-only runtime analysis.

## Implementation Notes
Pending analysis.

## Files Changed
- `plans/2026-09-02-airplay-session-log-analysis/plan.md`
- `plans/2026-09-02-airplay-session-log-analysis/steps/step-1.md`
