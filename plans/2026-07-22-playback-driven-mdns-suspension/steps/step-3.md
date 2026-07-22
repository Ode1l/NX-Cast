# Step 3: Documentation and Strict Build

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Document the corrected device experiment and produce a verified strict clean ID 12 NRO.

## Prerequisites
- Steps 1 and 2 completed.
- File to modify: `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md`.
- devkitPro MSYS2 and strict playback dependencies are installed.

## Deliverables
- Profile matrix and device steps distinguish ID 11 ownership/both-worker behavior from ID 12 playback/mDNS-only behavior.
- Strict clean Switch build and artifact hash for the user's next device run.
- After this step: the user can rebuild/upload ID 12 and report DLNA control, IPTV smoothness, AirPlay recovery, and heartbeat evidence.

## Plan
- [x] `edit` `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md` — add ID 12 expectations and corrected playback/return-home test sequence.
- [x] `bash` focused host regressions — zero failures.
- [x] `bash` strict devkitPro clean ID 12 build — generated `NX-Cast.nro` with all required dependencies.
- [x] `bash` profile consistency, NRO embedded marker, SHA-256, JSON parse, and `git diff --check` — final evidence recorded.

## Quality Checklist
- [x] Evidence-before-edit: current matrix/procedure and strict build workflow read.
- [x] Existing pattern / reuse checked: extend the established profile table, heartbeat fields, and result interpretation.
- [x] Contract understood: documentation does not claim SSDP is suspended in ID 12.
- [x] Risk reviewed: stale NRO, wrong picker/profile, path spaces, misleading ID 11 comparison.
- [x] Mitigation recorded: clean build, short MSYS path, embedded marker, and artifact hash.

## Validation Checklist
- [x] Strict clean ID 12 Switch build exits 0.
- [x] JSON/profile/diff checks exit 0.

## Test Checklist
- [x] Host coordinator and mDNS tests pass; device behavior is handed off explicitly.

## Implementation Notes
- Added an ID 12 device procedure that checks DLNA phone control, IPTV/DLNA smoothness, AirPlay rediscovery, retained ownership recovery, and all three suspension fields.
- Clean strict build passed with libmpv, deko3d, and AirPlay Ed25519 requirements enabled. The existing EGL/GLES pkg-config probe still prints warnings, but the deko3d build and final link succeed.
- `NX-Cast.nro` embeds `full-mdns-playback-suspend-bsd8`, is 25,600,698 bytes, and has SHA-256 `e5bfb5c53d701a6752f1fc060998da128d8b7a63d318acf933bc74299e6dcb39`.
- The VS Code picker parses and contains ID 12 exactly once; `git diff --check` passes with only existing LF-to-CRLF notices.
- Global reflection corrected the wiring-failure text to require the ID 12 tuple `discovery=1`, `mDNS=1`, `SSDP=0` rather than implying that all three flags should be one.

## Files Changed
- `docs/AIRPLAY_FREEZE_DIAGNOSTICS.md`
