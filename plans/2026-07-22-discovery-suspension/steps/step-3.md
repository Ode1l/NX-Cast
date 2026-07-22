# Step 3: BSD8 Diagnostic Profile and Device Handoff

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Wire the coordinator callback into a new BSD8-only diagnostic profile, expose evidence in logs/tasks/docs, and produce a strict clean Switch build for device testing.

## Prerequisites
- Steps 1 and 2 completed.
- Files to modify: `source/main.c`, `makefile`, `.vscode/tasks.json`, `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md`.
- devkitPro MSYS2 and strict playback dependencies are installed.

## Deliverables
- `full-discovery-suspend-bsd8` profile with eight BSD sessions and playback-driven discovery suspension.
- Runtime logging that shows requested/current discovery suspension state.
- Updated VS Code picker and device test procedure.
- After this step: strict clean NRO build succeeds and the user can run the new task on hardware.

## Plan
- [x] `read`/`rg` main operation initialization, heartbeat format, profile cases, task options, and diagnostic document conventions.
- [x] `edit` `source/main.c` — wire profile-gated coordinator callback to mDNS/SSDP setters and log suspended state.
- [x] `edit` `makefile` and `.vscode/tasks.json` — add unique profile ID/name, BSD8 macro, suspension macro, and picker option.
- [x] `edit` `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md` — document expected logs, protocol test order, and interpretation.
- [x] `bash` coordinator/discovery host regressions and strict devkitPro clean build — zero failures.
- [x] `bash` `rg` profile/macro across source/config/docs and hash the NRO — consistency and artifact evidence recorded.

## Quality Checklist
- [x] Evidence-before-edit: target wiring/config/docs read; profile identifiers searched; validation command fixed.
- [x] Existing pattern / reuse checked: extend existing diagnostic profile switch, VS Code input, runtime heartbeat, and document matrix.
- [x] Contract understood: only the new profile installs the callback; existing profiles are unchanged.
- [x] Risk reviewed: profile mismatch, stale incremental build, path spaces, missing strict dependencies.
- [x] Mitigation recorded: unique ID, `make clean`, TOPDIR/THIS_MAKEFILE short path workflow, strict require flags.

## Validation Checklist
- [x] All focused host tests exit 0.
- [x] Strict clean `full-discovery-suspend-bsd8` Switch build exits 0.
- [x] Profile identifiers appear consistently in Makefile, tasks, source, and docs.

## Test Checklist
- [ ] Idle homepage remains discoverable by AirPlay and DLNA. (device handoff)
- [ ] IPTV, DLNA, and AirPlay each log suspension after media claim and resume after final teardown/home return. (device handoff)
- [ ] Pause, seek, and buffering do not resume discovery. (device handoff)
- [ ] UI, X/B, nxlink heartbeat, first-frame time, stutter, and shutdown are recorded by the user. (device handoff)

## Implementation Notes
- Profile ID 11 enables full AirPlay, eight BSD service sessions, and installs the discovery callback; no existing profile installs it.
- Coordinator transition logs show `discovery_suspended=0->1/1->0`; two-second heartbeats show the coordinator request, both atomic worker flags, the mDNS phase, and existing counters.
- Focused host results: `protocol coordinator tests passed`; `AirPlay mDNS suspend tests passed`.
- The first strict build exposed GCC 16 removal of `ATOMIC_VAR_INIT`; using a constant `_Atomic bool = false` initializer resolved it. A second clean strict build completed and produced `NX-Cast.nro`.
- Artifact: size 25,600,698 bytes; SHA-256 `0287d7c573fe8e67e71ef9c1f0c755c7a870a8250f6311ab4b031817eba36e60`; `strings` contains `full-discovery-suspend-bsd8`.

## Files Changed
- `source/main.c`
- `makefile`
- `.vscode/tasks.json`
- `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md`
