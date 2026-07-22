# Step 5: Profile 13 and physical A/B contract

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Package the resource manager as an isolated Profile 13 using Borealis-like BSD settings and document a repeatable physical comparison against Profiles 8, 9, and 12.

## Prerequisites
- Steps 1-4 completed and host validation green.
- Files to modify: `makefile`, `.vscode/tasks.json`, `.vscode/launch.json` if selected-profile labels require it, and `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md`.
- Existing diagnostic profile matrix and VS Code input conventions have been read.

## Deliverables
- `full-owner-exclusive-bsd12` Profile ID 13 enables exclusive ownership, `num_bsd_sessions=12`, and `sb_efficiency=8` without changing IDs 1-12.
- VS Code can build/upload Profile 13 through the existing task/input workflow.
- Documentation defines expected Home/DLNA/AirPlay/IPTV service modes, transition log evidence, and repeated-play/seek/restore acceptance.
- After this step: profile validation and strict Switch compile reach link successfully.

## Plan
- [x] `edit makefile` — add Profile 13 macros for exclusive resources, BSD12, buffer efficiency 8, and profile validation.
- [x] `edit source/main.c` — apply socket overrides only when their macros are nonzero, log both values, and reject out-of-range overrides at compile time.
- [x] `edit .vscode/tasks.json .vscode/launch.json` — expose Profile 13 as the existing selector default without changing the MSYS path fix.
- [x] `edit docs/AIRPLAY_FREEZE_DIAGNOSTICS.md` — add ID 13 matrix, expected state transitions, log fields, A/B order, and pass/fail checklist.
- [x] `bash make clean && make NXCAST_DIAG_PROFILE=full-owner-exclusive-bsd12 ... -j4` — clean compile and link succeeds.

## Quality Checklist
- [x] Evidence-before-edit: current profile definitions/tasks/docs read and all profile-name consumers searched.
- [x] Existing pattern / reuse checked: extended the diagnostic selector rather than altering normal build behavior.
- [x] Contract understood: Profile 13 changes two BSD parameters plus the explicitly named resource policy; old profiles remain valid controls.
- [x] Risk reviewed: invalid libnx session range, Application/Applet memory difference, JSON quoting, stale documentation.
- [x] Mitigation recorded: compile-time bounds, logged config, preserved IDs, clean build, and a physical A/B matrix.

## Validation Checklist
- [x] Clean strict Profile 13 Switch build exits 0 and produces `NX-Cast.nro`.
- [x] `python -m json.tool .vscode/tasks.json` and `.vscode/launch.json` exit 0 using the bundled Python runtime.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] Unknown profile still fails with a complete expected-profile list that includes Profile 13.
- [x] Profile 13 binary contains the profile name, network heartbeat/stall strings, and IPTV/DLNA/AirPlay exclusive-mode names.
- [x] Profiles 1–12 retain their prior macros and IDs; all exclusive behavior remains gated by the new Profile 13 macro.

## Implementation Notes
- `full-owner-exclusive-bsd12` is Profile ID 13 and defines `NXCAST_SOCKET_BSD_SESSIONS=12`, `NXCAST_SOCKET_SB_EFFICIENCY=8`, and `NXCAST_EXCLUSIVE_MEDIA_RESOURCES=1`.
- The profile picker defaults to ID 13 for the next hardware run but retains every earlier profile as a control.
- The playbook specifies Home/DLNA/AirPlay/IPTV service sets, five repeated DLNA cycles, seek/control checks, cross-protocol restoration, and objective log failure conditions.
- EGL/GLES pkg-config warnings remain harmless because the required deko3d path compiles and the NRO links successfully.

## Files Changed
- `makefile`
- `source/main.c`
- `.vscode/tasks.json`
- `.vscode/launch.json`
- `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md`
