# Step 2: Profile ID 12 Runtime Wiring

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Add ID 12 and drive mDNS-only suspension from the latest player snapshot while leaving ID 11 unchanged.

## Prerequisites
- Step 1 completed with both coordinator policies tested.
- Files to modify: `source/main.c`, `makefile`, `.vscode/tasks.json`.

## Deliverables
- `full-mdns-playback-suspend-bsd8` Profile ID 12 with BSD8 and a distinct macro.
- ID 12 callback touches mDNS only; ID 11 callback still touches mDNS and SSDP.
- Player activity is synchronized before coordinator transition logging and runtime heartbeat capture.
- After this step: task JSON parses, profile consistency checks pass, and focused host regressions remain green.

## Plan
- [x] `edit` `source/main.c` — add separate callback wiring/policy config and feed `main_snapshot_playback_active()` after every snapshot.
- [x] `edit` `makefile` — register Profile ID 12 with BSD8 and `NXCAST_SUSPEND_AIRPLAY_MDNS_WHILE_PLAYBACK=1` while preserving ID 11.
- [x] `edit` `.vscode/tasks.json` — add the new picker option exactly once.
- [x] `bash` JSON/profile consistency and focused host targets — zero failures.

## Quality Checklist
- [x] Evidence-before-edit: main loop ordering, existing predicate, callbacks, Makefile cases, and picker read.
- [x] Existing pattern / reuse checked: reuse `main_snapshot_playback_active()` rather than duplicate state logic.
- [x] Contract understood: ID 12 updates only mDNS; SSDP flag stays zero; ID 11 behavior is unchanged.
- [x] Risk reviewed: one-frame lag, stale snapshot, pre-start calls, profile macro collision.
- [x] Mitigation recorded: API ignores irrelevant policy/runtime and main refreshes coordinator snapshot before logging.

## Validation Checklist
- [x] `.vscode/tasks.json` parses and contains ID 12 once.
- [x] Profile/macro appear consistently in source/config.

## Test Checklist
- [x] Coordinator and mDNS focused host tests exit 0.

## Implementation Notes
- ID 12 uses the existing `main_snapshot_playback_active()` predicate, so loading/buffering/seeking/playing/paused suspend and stopped/idle/error resume on the next main-loop snapshot.
- The snapshot is now read before the coordinator update and transition logging, removing the previous one-frame stale diagnostic ordering.
- The ID 12 callback calls only `airplay_mdns_set_suspended()`. The separate ID 11 callback still calls both mDNS and SSDP setters.
- JSON parsing found exactly one ID 12 picker value; coordinator and mDNS host regressions both passed.

## Files Changed
- `source/main.c`
- `makefile`
- `.vscode/tasks.json`
