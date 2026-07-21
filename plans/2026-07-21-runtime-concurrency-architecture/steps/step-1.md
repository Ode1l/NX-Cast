# Step 1: Define The Runtime Contract

> Status: COMPLETED
> Created: 2026-07-21

## Goal
Replace the inaccurate threading documentation with an explicit, auditable ownership, message-flow, overload, and shutdown contract before runtime code changes.

## Prerequisites
- Current thread creators and direct player call sites have been inventoried.
- Existing `docs/threading-design.md`, `docs/player-layer.md`, logger queue, player session, and coordinator have been read.
- Design: user explicitly requested design-first multithread management modeled on the logging system.

## Deliverables
- `docs/threading-design.md` distinguishes current violations from the target actor/supervisor architecture.
- `docs/player-layer.md` documents asynchronous command submission and the render/control ownership split.
- After this step: every later code change can be checked against named invariants and prohibited call paths.

## Plan
- [x] `edit` `docs/threading-design.md` — define actors, thread/resource ownership table, command/event flow, queue policy, lock rules, lifecycle FSM, and shutdown phases.
- [x] `edit` `docs/player-layer.md` — replace the direct command path with the target Media Actor API and compatibility migration notes.
- [x] `bash` `rg` thread and direct-call inventories — record current violations as migration targets rather than claiming the target already exists.
- [x] `bash` `git diff --check` — verify documentation formatting.

## Quality Checklist
- [x] Evidence-before-edit: read target docs/source, searched all player mutations/thread creators, validation is `git diff --check`.
- [x] Existing pattern / reuse checked: logger queue and player event thread will be extended rather than creating duplicate infrastructure.
- [x] Contract understood: main owns render; Media Actor owns control/events; protocols own parsing/network only.
- [x] Risk reviewed: deadlock, starvation, queue exhaustion, stale work, shutdown and render-thread affinity.
- [x] Mitigation recorded: bounded queue, no external calls under locks, generation validation, health snapshot, phased migration.

## Validation Checklist
- [x] `git diff --check` exits 0.
- [x] Thread inventory in the plan matches current source call sites.

## Test Checklist
- [x] N/A — documentation-only contract; executable tests begin in Step 2.

## Implementation Notes
The old document asserted that a player owner thread already executed commands. Source inspection disproved that statement, so the replacement labels the actor model as target architecture and lists current violations. The design reuses the existing player event thread and logger producer/consumer pattern rather than adding parallel owner threads.

## Files Changed
- `docs/threading-design.md`
- `docs/player-layer.md`
- `plans/2026-07-21-runtime-concurrency-architecture/plan.md`
- `plans/2026-07-21-runtime-concurrency-architecture/steps/step-1.md`
