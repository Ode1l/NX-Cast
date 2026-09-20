# Step 1: Locate Earliest Regressions

> Status: COMPLETED
> Created: 2026-09-02

## Goal
Classify the earliest AirPlay and IPTV divergence by comparing current code, `v0.2.0`, and the latest mixed-protocol trace.

## Prerequisites
- Baseline tag `v0.2.0` exists.
- Latest mixed-protocol trace exists under `logs/`.
- Files to inspect: shared player options, IPTV playback, AirPlay routes, bridge, and coordinator ownership.

## Deliverables
- An evidence table mapping each failure to negotiation, bridge, player policy, transport, or source supply.
- After this step: implementation changes are limited to verified regression points.

## Plan
- [x] `rg` `source/player`, `source/iptv`, and `source/protocol/airplay` — map media policy selection and command submission.
- [x] `git diff` current paths against `v0.2.0` — identify shared libmpv/HLS changes that can affect IPTV.
- [x] `rg` `logs/run_nxlink-20260902-012957.log` — correlate the first protocol/media error per session.
- [x] `edit` plan files — record only verified causes and exact implementation targets.

## Quality Checklist
- [ ] Evidence-before-edit: target reads and impact searches complete; validation commands discovered.
- [ ] Existing pattern / reuse checked: media-profile and ownership helpers searched before adding behavior.
- [ ] Contract understood: protocol intent, media supply, and player execution remain separate lifetimes.
- [ ] Risk reviewed: shared FFmpeg options, stale ownership, live-playlist starvation, and worker capacity.
- [ ] Mitigation recorded: protocol-neutral policy selection and focused tests.

## Validation Checklist
- [ ] Every proposed source change maps to an earlier log or baseline divergence.
- [ ] DLNA multithreading is excluded unless direct blocking evidence appears.

## Test Checklist
- [ ] N/A — diagnostic step; focused tests are added or run in implementation steps.

## Implementation Notes
- The first IPTV divergence is `a9a8e62`: all network media began receiving an explicit 20-second cache instead of mpv defaults.
- The live IPTV trace shows segment expiry and exact 20-second PTS jumps without socket, actor, or coordinator pressure.
- AirPlay requests reach `/play`; current uncommitted reverse-HLS work addresses H.264 variant selection and FFmpeg cross-host connection reuse but has not yet been represented by a device trace.
- Current and `v0.2.0` DLNA HTTP servers are both serialized, so a worker pool is not a regression fix.

## Files Changed
- `plans/2026-09-02-airplay-hls-iptv-regression/plan.md`
- `plans/2026-09-02-airplay-hls-iptv-regression/steps/step-1.md`
