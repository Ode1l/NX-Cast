# Step 2: Introduce The Media Actor

> Status: COMPLETED
> Created: 2026-07-21

## Goal
Turn the existing player event worker into the sole executor for bounded media commands while preserving the public player behavior through compatibility adapters.

## Prerequisites
- Step 1 completed and thread ownership invariants are documented.
- Files to modify: `source/player/core/session.c`, new focused player-core queue files if needed, player headers, Makefile, and host tests.

## Deliverables
- Fixed-capacity command queue with deep-copied payloads, source/generation metadata, completion status, and health counters.
- Media Actor drains commands and pumps backend events without holding queue/snapshot locks during backend calls.
- After this step: mock-backend tests prove one executor, FIFO ordering, queue-full behavior, timeout, and clean stop.

## Plan
- [x] `write` focused actor tests — establish ordering, single executor, saturation, timeout, and stop contracts before implementation.
- [x] `edit` player core/API — add bounded submission and actor execution using the existing event thread.
- [x] `edit` Makefile — add `test-player-actor` and include it in host regression.
- [x] `bash` focused tests, sanitizers, and `git diff --check` — require all pass.

## Quality Checklist
- [x] Evidence-before-edit: read current player session/backend contracts and all public callers.
- [x] Existing pattern / reuse checked: reuse logger queue mechanics and current player event thread.
- [x] Contract understood: queue lock protects storage only; backend execution is actor-only and lock-free externally.
- [x] Risk reviewed: payload ownership, timeout lifetime, lost wakeup, queue shutdown, event ordering.
- [x] Mitigation recorded: fixed capacity, explicit completion ownership, condvar tests, ASan/TSan.

## Validation Checklist
- [x] `make test-player-actor` exits 0.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] Focused actor tests and available sanitizers pass.

## Implementation Notes
Added a portable opaque Media Actor with a fixed 64-entry queue, four reserved shutdown slots, deep-copied command payloads, single executor identity, bounded synchronous compatibility waits, timeout-safe request references, health counters, and idempotent concurrent stop. The existing player event worker is replaced by this actor; all control methods now enqueue before reaching the backend, while deko3d render calls remain main-thread-owned. ASan/UBSan and TSan focused runs pass. A WIP commit was intentionally skipped because the worktree contains unrelated in-progress AirPlay changes that must remain untouched.

## Files Changed
- `source/player/core/media_actor.h`
- `source/player/core/media_actor.c`
- `source/player/core/session.c`
- `scripts/test_media_actor.c`
- `Makefile`
