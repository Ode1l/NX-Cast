# Step 2: Add BSD16 Capacity Comparison

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Produce a receive-only diagnostic NRO with sixteen BSD service sessions for a controlled comparison against BSD8.

## Prerequisites
- Step 1 completed and BSD8 device testing restored connectivity but left reproducible DLNA stalls.
- Files to modify: `makefile`, `.vscode/tasks.json`, and `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md`.
- Design: change only `NXCAST_SOCKET_BSD_SESSIONS` from 8 to 16 relative to the BSD8 profile.

## Deliverables
- A selectable and documented `mdns-receive-bsd16` Profile.
- After this step: a strict clean build produces a profile-specific NRO for device testing.

## Plan
- [x] `edit` `makefile` — add the BSD16 receive-only Profile with a unique ID.
- [x] `edit` `.vscode/tasks.json` — expose BSD16 beside BSD8 in the existing picker.
- [x] `edit` `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md` — document the capacity comparison and stop condition.
- [x] `bash` strict clean BSD16 build — require successful NRO generation.
- [x] `bash` JSON parsing, binary markers, and `git diff --check` — require zero errors.

## Quality Checklist
- [x] Evidence-before-edit: BSD8 target/profile paths read; device log compared; strict build command identified.
- [x] Existing pattern / reuse checked: reuse the compile-time socket-session override from Step 1.
- [x] Contract understood: BSD8 and BSD16 differ only in Profile identity and session count.
- [x] Risk reviewed: larger socket work buffer and diminishing value from further pool increases.
- [x] Mitigation recorded: diagnostic-only profile; if BSD16 fails, stop increasing sessions and redesign blocking socket usage.

## Validation Checklist
- [x] Strict `mdns-receive-bsd16` clean build exits 0 and produces `NX-Cast.nro`.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] VS Code JSON parses and BSD16 appears exactly once.
- [x] Binary markers contain `mdns-receive-bsd16` and the shared socket-session log format.

## Implementation Notes
Added Profile ID 10 using the existing receive-only mDNS mode and default priority, with only `NXCAST_SOCKET_BSD_SESSIONS` changed from 8 to 16. The strict clean build exited 0 and produced an NRO with the expected Profile and socket-session log markers.

## Files Changed
- `makefile`
- `.vscode/tasks.json`
- `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md`
- `plans/2026-07-22-bsd-session-diagnostic/plan.md`
- `plans/2026-07-22-bsd-session-diagnostic/steps/step-2.md`
