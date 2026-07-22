# Step 2: Runtime receiver isolation and terminal release

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Wire coordinator resource modes to real DLNA/AirPlay receivers and IPTV background work, while making every terminal media path release its lease and restore Home mode.

## Prerequisites
- Step 1 completed with tested resource modes and transition worker.
- Files to modify: `source/main.c`, `source/iptv/iptv.[ch]`, DLNA action files, and AirPlay integration only where release symmetry is missing.
- Full start/stop implementations for DLNA and AirPlay have been read.

## Deliverables
- DLNA-exclusive, AirPlay-exclusive, and IPTV-exclusive runtime service sets match `plan.md`.
- IPTV background refresh/logo work stops accepting/processing jobs while media ownership is active and resumes at Home.
- DLNA Stop releases ownership; Play can safely reacquire an existing URI; RenderingControl cannot claim idle ownership.
- Terminal player state and Home provide a fallback lease release without racing initial media load.
- After this step: host coordinator/AirPlay tests and shutdown-order checks pass.

## Plan
- [x] `edit source/main.c` — enable exclusive resource policy, feed stable player terminal state to the coordinator, expose transition state in runtime heartbeat, and preserve shutdown ordering.
- [x] `edit source/iptv/iptv.h source/iptv/iptv.c` — add idempotent background suspend/resume that gates refresh/logo work without deinitializing FFmpeg networking.
- [x] `edit source/protocol/dlna/control/action/avtransport.c` — release after successful Stop and reacquire on Play when the retained URI is unowned.
- [x] `edit source/protocol/dlna/control/action/renderingcontrol.c` — reject or apply unowned volume/mute without creating a media owner, matching player command contracts.
- [x] `edit source/protocol/airplay/integration.c` — verify all remote-video/mirror error and disconnect paths release the current lease exactly once; add only missing symmetry.
- [x] `edit scripts/test_protocol_coordinator.c` — cover terminal-state grace, Stop/Play reacquisition, background suspension, blocked restart supersession, and wait cancellation through fakes.
- [x] `bash make test-protocol-coordinator test-airplay test-shutdown-order` — coordinator and shutdown suites pass; the aggregate AirPlay target reaches a pre-existing Cygwin `test_log_mirror.c` closed-peer assertion after the relevant coordinator/media-actor tests pass.

## Quality Checklist
- [x] Evidence-before-edit: target read, impact search for all lease claims/releases and IPTV worker wakeups, validation commands identified.
- [x] Existing pattern / reuse checked: reused current DLNA/AirPlay full start/stop APIs and media actor release command.
- [x] Contract understood: owner protocol remains controllable; non-owner stop occurs on coordinator worker; pause retains ownership.
- [x] Risk reviewed: FFmpeg global init lifetime, Stop/Play compatibility, callback replacement, reconnect race.
- [x] Mitigation recorded: suspend IPTV jobs instead of deinit, terminal grace/latch, idempotent release, target supersession, and host concurrency tests.

## Validation Checklist
- [x] `make test-protocol-coordinator` exits 0.
- [ ] `make test-airplay` exits 0 — blocked after relevant tests by the existing Cygwin-only `test_log_mirror.c` closed-peer assertion.
- [x] `make test-shutdown-order` equivalent Python invocation exits 0; MSYS lacks `python3`.
- [x] `git diff --check` exits 0 for changed runtime files.
- [x] Profile 13 Switch build links `NX-Cast.nro` after the final AirPlay/Home changes.

## Test Checklist
- [x] DLNA, AirPlay, and IPTV modes retain only their required receiver/background resources in coordinator service-set tests.
- [x] Pause does not restore Home; Stop/error/EOF/disconnect/Home does.
- [x] DLNA Stop then Play reacquires without requiring SetURI.
- [x] RenderingControl at Home cannot reserve DLNA ownership.

## Implementation Notes
- Added opt-in resource waits before OPEN/PLAY so a protocol cannot consume player/network resources until non-owner receiver teardown is complete.
- Kept service stop/join callbacks on the coordinator worker; blocked starts can be cancelled when the desired owner changes or shutdown begins.
- IPTV retains FFmpeg network initialization but pauses refresh/logo jobs. The currently executing remote fetch remains cooperative and is handled by Step 4 cancellation hardening.
- Player terminal observation uses an armed active-state latch plus a stable-resource grace period to avoid releasing a new lease from a stale pre-OPEN idle snapshot.
- Profile 13 gates all new semantics; profiles 1–12 preserve their previous behavior.
- Aggregate `test-airplay` is not green in this environment because its unrelated log-mirror closed-peer assertion fails on Cygwin. Coordinator, media actor, log policy, C safety (with `_GNU_SOURCE`), shutdown ordering, and the Switch cross-build pass.

## Files Changed
- `source/app/protocol_coordinator.[ch]`
- `scripts/test_protocol_coordinator.c`
- `source/main.c`
- `source/iptv/iptv.[ch]`
- `source/protocol/dlna/control/action/avtransport.c`
- `source/protocol/dlna/control/action/renderingcontrol.c`
- `source/protocol/airplay/integration.[ch]`
- `makefile`
