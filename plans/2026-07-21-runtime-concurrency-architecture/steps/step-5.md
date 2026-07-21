# Step 5: Add Health And Deterministic Shutdown

> Status: COMPLETED
> Created: 2026-07-21

## Goal
Make actor/supervisor health and teardown progress observable and guarantee that shutdown stops producers before destroying their dependencies.

## Prerequisites
- Steps 2-4 completed with actor and supervisor snapshots.
- Current persistent logger health snapshot and shutdown trace are available for reuse.

## Deliverables
- Health snapshot includes queue depth/high-water mark, rejected/coalesced commands, current command age, actor heartbeat, and service transition age.
- Shutdown phases cancel new submissions, stop protocol producers, drain/cancel media, detach render, destroy backend, flush logger, then release network/platform.
- After this step: fault tests identify the exact stuck phase without relying on nxlink survival.

## Plan
- [x] `edit` actor/coordinator/log snapshots — add bounded health fields and phase logs.
- [x] `edit` shutdown orchestration — enforce dependency phases and idempotence.
- [x] `write` fault-injection tests — queue full, blocked completion, shutdown during load, and logger mirror failure.
- [x] `bash` host tests, sanitizers, strict Switch build, and static shutdown-order audit.

## Quality Checklist
- [x] Evidence-before-edit: shutdown callers/resources and existing logger stats read.
- [x] Existing pattern / reuse checked: extend runtime heartbeat and logger stats rather than duplicate telemetry.
- [x] Contract understood: after quiesce begins no producer may enqueue new media work.
- [x] Risk reviewed: join deadlock, use-after-free, dropped critical stop, logs after logger shutdown.
- [x] Mitigation recorded: phase FSM, bounded waits, cancellation, persistent phase markers.

## Validation Checklist
- [x] Fault tests report deterministic phase/reason.
- [x] Full host suite, sanitizers, formatting, and strict Switch build pass.

## Test Checklist
- [x] Shutdown/load/queue/logger failure matrix passes.

## Implementation Notes
Actor health now reports lifecycle/dispatch state, queue high-water mark,
coalescing/rejection/timeout counters, active-command age, and heartbeat age.
Coordinator snapshots include per-service worker/transition age; logger stats
include queue high-water mark, worker heartbeat, and waiting state. The nxlink
mirror socket is configured nonblocking and backpressure is dropped/countable.
Shutdown order is statically tested and waits for actor drain before render and
backend teardown. Backend init/finalize also moved onto the actor with a tested
render activation gate.

## Files Changed
- `source/player/core/media_actor.c`
- `source/player/core/media_actor.h`
- `source/player/core/session.c`
- `source/player/player.h`
- `source/app/protocol_coordinator.c`
- `source/app/protocol_coordinator.h`
- `source/log/log.c`
- `source/log/log.h`
- `source/log/mirror.c`
- `source/log/mirror.h`
- `source/main.c`
- `scripts/test_media_actor.c`
- `scripts/test_log_mirror.c`
- `scripts/test_shutdown_order.py`
- `makefile`
