# Step 1: Reconstruct Failure Timelines

> Status: COMPLETED
> Created: 2026-09-02

## Goal
Produce evidence-backed timelines and root-cause classifications for the latest YouTube, Bilibili, and IPTV attempts.

## Prerequisites
- Latest trace exists at `logs/run_nxlink-20260902-010123.log`.
- No source edits are allowed until causal events are identified.

## Deliverables
- A timestamped session map from `/play` or IPTV selection through player/control termination.
- After this step: Step 2 has explicit files, behavior, and regression tests to change.

## Plan
- [x] `rg` `logs/run_nxlink-20260902-010123.log` — index media sequence, owner generation, session, URL hash, file-loaded, buffering, TLS, decoder, and terminal events.
- [x] `read` bounded ranges in `logs/run_nxlink-20260902-010123.log` — correlate each warning/error with its active media sequence.
- [x] `rg` relevant source and historical logs — compare each earliest divergence with receiver/player behavior and known-good traces.
- [x] `edit` this plan — record verified causes, non-causes, and exact Step 2 targets.

## Quality Checklist
- [x] Evidence-before-edit: latest trace read, source impact search completed, focused and aggregate validation identified.
- [x] Existing pattern / reuse checked: historical known-good NX-Cast lifecycle traces and the existing protocol media event model were used.
- [x] Contract understood: control, bridge, player, and decoder lifetimes remain separate.
- [x] Risk reviewed: false attribution from interleaved sessions and downstream decoder noise.
- [x] Mitigation recorded: correlated by sequence, generation, session, URL hash, and timestamp.

## Validation Checklist
- [x] Every reported root cause cites an earlier event than its downstream symptom.
- [x] Every proposed edit maps to a receiver-controlled behavior.

## Test Checklist
- [ ] N/A — analysis step; Step 2 supplies focused tests for selected behavior.

## Implementation Notes
YouTube's first decoder corruption precedes the TLS EOF. The receiver holds local playlist keep-alive sockets throughout playback despite a constrained BSD session pool. Bilibili exposes an independent state regression: final control close no longer submits `CONTROL_DETACHED`, although media correctly remains active. Step 2 therefore changes only local playlist connection lifetime and control-state notification.

## Files Changed
- `plans/2026-09-02-media-regression-triage/plan.md`
- `plans/2026-09-02-media-regression-triage/steps/step-1.md`
