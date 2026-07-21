# Step 2: Hang-Boundary Trace

> Status: COMPLETED
> Created: 2026-07-21

## Goal
Emit ordered runtime events that identify where IPTV loading and return-home stop progressing.

## Prerequisites
- Step 1 completed with a durable SD sink.
- Files to modify: `source/main.c`, `source/player/core/session.c`, `source/iptv/iptv.c` as evidence requires.

## Deliverables
- Periodic main-loop/logger health heartbeat in trace builds.
- Begin/done/failure events around return-home, player stop dispatch, IPTV set-uri/play, and pending-home completion.
- After this step: the last persisted event names the blocking boundary.

## Plan
- [x] `read` `source/main.c`, `source/player/core/session.c`, and `source/iptv/iptv.c` — re-confirm exact synchronous boundaries.
- [x] `edit` relevant files — add correlated `INFO` begin/done events and periodic queue/sink health without frame-level spam.
- [x] `rg` source trace messages — verify each blocking call has a preceding event and successful calls have a paired result.
- [x] `bash` `git diff --check` — verify patch hygiene.

## Quality Checklist
- [x] Evidence-before-edit: targets re-read, shared callers searched, validation identified.
- [x] Existing pattern / reuse checked: used existing media sequence and monotonic clock helpers.
- [x] Contract understood: diagnostics do not alter player/UI decisions or command order.
- [x] Risk reviewed: logging noise and timing perturbation.
- [x] Mitigation recorded: two-second trace-build heartbeat and event-only boundary logs.

## Validation Checklist
- [x] Every synchronous home/stop/load boundary has begin and terminal event text.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] Existing host tests remain scheduled in Step 3.

## Implementation Notes
Added SD-visible health counters to a two-second Full Trace heartbeat. Return-home now logs before and after synchronous stop/show-home calls. Player backend operations log backend, snapshot refresh, and wakeup phases. Libmpv logs before and after its mutex acquisition. IPTV uses one media sequence across claim, old-stream stop, set-uri, and play stages. Diagnostic traces are `INFO`; actual failures remain `WARN`/`ERROR`.

## Files Changed
- `source/log/log.h`
- `source/log/log.c`
- `source/main.c`
- `source/player/trace.c`
- `source/player/core/session.c`
- `source/player/backend/libmpv.c`
- `source/protocol/airplay/trace.h`
- `source/iptv/iptv.c`
