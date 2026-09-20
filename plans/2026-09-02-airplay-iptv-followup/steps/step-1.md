# Step 1: Reconstruct Latest Failure Timelines

> Status: COMPLETED
> Created: 2026-09-02

## Goal
Produce evidence-backed timelines and root-cause classifications for the latest YouTube, Bilibili, and IPTV attempts.

## Prerequisites
- Latest trace exists at `logs/run_nxlink-20260902-012957.log`.
- No source edits are made until initiating failures are identified.

## Deliverables
- Session map covering play request, network supply, file load, decode, buffering, control transitions, and termination.
- Exact Step 2 targets and test contracts, or a report that more device evidence is required.

## Plan
- [x] `rg` the latest trace — index media sequence, logical session, URL mode, local playlist connections, TLS, decoder, seek, buffering, and terminal events.
- [x] `read` bounded trace ranges — correlate each warning/error with its active session.
- [x] `rg` current source and known-good traces — identify the first receiver-controlled divergence.
- [x] `edit` plan evidence — record causes, non-causes, and Step 2 scope.

## Quality Checklist
- [ ] Evidence-before-edit: latest trace read, impact search pending, validation selected after classification.
- [ ] Existing pattern / reuse checked: current state/transport code and known-good traces compared.
- [ ] Contract understood: protocol, transport, player, demux, and decoder lifetimes remain separate.
- [ ] Risk reviewed: interleaved sessions and downstream warning misattribution.
- [ ] Mitigation recorded: correlate by sequence, session, generation, URL mode, and timestamp.

## Validation Checklist
- [ ] Every classification cites an earlier causal event than its downstream symptoms.
- [ ] Every proposed edit maps to receiver-controlled behavior.

## Test Checklist
- [ ] N/A — analysis step; Step 2 defines focused regression tests.

## Implementation Notes
Step 2 will disable HLS HTTP persistence only for the local reverse-HLS player profile and prefer H.264 variants when the supplied master explicitly offers H.264 alongside other video codecs. No sender-specific, global network, IPTV cache, or AirPlay lifecycle change is justified.

## Files Changed
- `plans/2026-09-02-airplay-iptv-followup/plan.md`
- `plans/2026-09-02-airplay-iptv-followup/steps/step-1.md`
