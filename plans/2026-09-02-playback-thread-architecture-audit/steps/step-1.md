# Step 1: Compare Playback Resource Lifetimes

> Status: COMPLETED
> Created: 2026-09-02

## Goal
Produce an evidence-backed classification of current failures and explain how playback threads were handled before and after the AirPlay refactor.

## Prerequisites
- Baseline tag `v0.2.0` exists.
- Latest mixed-protocol log is available locally.
- Analysis only; no production source modification is authorized in this step.

## Deliverables
- Historical/current thread and socket ownership comparison.
- Root-cause classification with confidence and residual unknowns.
- After this step: the next repair or test can target one layer without speculative cross-layer changes.

## Plan
- [x] `rg` current `source/` — map thread creation, listener ownership, connection workers, player actor, and coordinator events.
- [x] `git show` `v0.2.0` source paths — reconstruct pre-AirPlay DLNA/IPTV/player threading.
- [x] `rg` `logs/run_nxlink-20260902-012957.log` — correlate socket pressure, worker counts, coordinator transitions, and media failures.
- [x] `edit` plan files — record verified facts and final classification only.

## Quality Checklist
- [x] Evidence-before-edit: current and historical targets read; impact is analysis-only; runtime trace selected.
- [x] Existing pattern / reuse checked: v0.2.0 and current architecture compared directly.
- [x] Contract understood: protocol control and media/player lifetimes remain separate.
- [x] Risk reviewed: interleaved protocol sessions can cause false attribution.
- [x] Mitigation recorded: correlate by media sequence, owner generation, session ID, and earliest error.

## Validation Checklist
- [x] Every conclusion cites source/history or runtime evidence.
- [x] Unknown behavior is labelled unknown rather than inferred as fact.

## Test Checklist
- [ ] N/A — read-only architecture audit.

## Implementation Notes
- The current architecture separates network receivers from player execution. A single media actor serializes libmpv commands; it does not own or suspend network sockets.
- DLNA HTTP remains a one-connection-at-a-time listener, matching the `v0.2.0` behavior. This is a scalability risk but not a new regression.
- AirPlay control and loopback reverse-HLS connections use separate bounded worker pools. The latest trace shows no capacity rejection and balanced worker cleanup.
- With `NXCAST_KEEP_RECEIVERS_DURING_MEDIA=1`, media ownership transitions remain `resource=home->home`; no coordinator resource worker tears down receivers during playback.
- Latest runtime evidence rules out observed NX-Cast socket exhaustion or actor/coordinator blockage. FFmpeg-owned sockets are outside the instrumented count, but their errors identify cross-host HLS reuse and expired playlist segments rather than global socket starvation.
- Failure classification: YouTube is post-protocol HLS/demux/codec policy; failed Bilibili attempts are AirPlay negotiation/interoperability before video submission; IPTV is live-source/playlist starvation in this trace.

## Files Changed
- `plans/2026-09-02-playback-thread-architecture-audit/plan.md`
- `plans/2026-09-02-playback-thread-architecture-audit/steps/step-1.md`
