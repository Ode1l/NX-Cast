# Step 1: Durable Log Sinks

> Status: COMPLETED
> Created: 2026-07-21

## Goal
Make the async logger persist records to SD and mirror to nxlink without blocking on a dead peer.

## Prerequisites
- Latest log and logger implementation inspected.
- Files to modify: `source/log/log.c`, `source/log/log.h`, `source/main.c`.
- Existing dirty worktree must remain intact; protection commit/stash is skipped to avoid capturing user changes.

## Deliverables
- Public logger configuration for persistent file and socket mirror.
- Current/previous runtime log rotation under `sdmc:/switch/NX-Cast/logs`.
- After this step: records continue reaching SD after nxlink failure.

## Plan
- [x] `edit` `source/log/log.h` — add file-sink/socket-mirror configuration and health stats API.
- [x] `edit` `source/log/log.c` — write SD first, use non-blocking socket send, detect mirror failure, and expose counters.
- [x] `edit` `source/main.c` — create/rotate the log directory and configure sinks after storage setup.
- [x] `bash` `git diff --check` — verify patch hygiene.

## Quality Checklist
- [x] Evidence-before-edit: targets read; impact search `rg -n "log_set_stdio_mirror|log_runtime"`; validation commands recorded in plan.
- [x] Existing pattern / reuse checked: existing async queue and history are extended rather than replaced.
- [x] Contract understood: logging remains non-blocking for callers; shutdown owns final sink close.
- [x] Risk reviewed: performance, data durability, socket failure, and SD availability.
- [x] Mitigation recorded: worker-only I/O, bounded rotation, non-blocking network mirror, no sensitive payload additions.

## Validation Checklist
- [x] `git diff --check` exits 0.
- [x] Incremental Switch build exits 0; clean Full Trace build remains in Step 3.

## Test Checklist
- [x] `make test-protocol-coordinator` remains scheduled in Step 3.
- [x] Manual fault behavior is instrumented for next real-device run.

## Implementation Notes
Extended the existing async worker instead of adding a second logger. SD is written and flushed before a best-effort `MSG_DONTWAIT` nxlink send. Congestion increments mirror-drop stats; terminal socket errors disable only the mirror. `runtime.log` rotates to `runtime.previous.log` at startup.

## Files Changed
- `source/log/log.h`
- `source/log/log.c`
- `source/main.c`
