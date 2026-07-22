# Step 2: End Abandoned Polled DLNA Sessions

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Turn cessation of an established DLNA polling session into a Profile 13 return-home request without changing explicit Stop or non-polling renderer behavior.

## Prerequisites
- Step 1 implementation and target build completed; the pre-existing host AirPlay dependency block is documented.
- Files to modify: `source/protocol/dlna/control/action/avtransport.c`, `source/protocol/dlna/control/controller_session.c`, `source/protocol/dlna/control/controller_session.h`, `source/main.c`, `scripts/test_dlna_controller_session.c`, `makefile`.
- Design: four recurring read-only queries arm tracking; ten seconds of silence expires only while DLNA owns active/paused/loading media.

## Deliverables
- A small lock-free controller-session tracker with deterministic host tests.
- AVTransport query/reset hooks and Profile 13 main-loop integration through `main_request_player_home()`.
- After this step: established controller disappearance requests stop/home once, while explicit Stop and unarmed playback retain prior behavior.

## Plan
- [x] `write` `scripts/test_dlna_controller_session.c` — add deterministic cases for arming, refresh, timeout, reset, owner loss, sparse queries, explicit Stop, and retry.
- [x] `write` `source/protocol/dlna/control/controller_session.h` and `source/protocol/dlna/control/controller_session.c` — implement atomic polling-session and remote-home event state with caller-supplied timestamps.
- [x] `edit` `makefile` — add a space-safe `test-dlna-controller-session` target and enable the 10000 ms exit timeout only for Profile 13.
- [x] `bash` `make test-dlna-controller-session` — prove the pure state machine passes before and after wiring side effects.
- [x] `edit` `source/protocol/dlna/control/action/avtransport.c` — reset on new URI, request home on successful Stop, arm on successful transport/position queries, and refresh on successful commands.
- [x] `edit` `source/main.c` — consume explicit/timeout events and reuse the existing asynchronous return-home path with reasoned logging and retry.
- [x] `bash` focused controller/coordinator/shutdown tests plus Profile 13 target build — verify state, ownership, shutdown, and platform compilation.

## Quality Checklist
> Evidence summary only. Detailed guidance lives in `references/code-quality.md`, `references/risk-classification.md`, and `references/verify-step.md`.

- [x] Evidence-before-edit: target read `avtransport.c`/`main.c`, impact search `rg "GetTransportInfo|GetPositionInfo|main_request_player_home"`, validation focused host tests and target build
- [x] Existing pattern / reuse checked: reuse `main_request_player_home()` and coordinator terminal release; no new watchdog thread or socket-close inference
- [x] Contract understood: SOAP callbacks only publish liveness/home events; main loop owns stop/home side effects; explicit Stop stays immediate
- [x] Risk reviewed: correctness / API-contract / performance / observability / project-fit
- [x] Mitigation recorded: four-query arming, 10-second threshold, C11 atomics, one-shot consumption/reset/retry, compile-time Profile 13 gate

## Validation Checklist
- [x] `make test-dlna-controller-session` exits 0 repeatedly.
- [x] Coordinator test and bundled-Python shutdown-order test exit 0.
- [x] Profile 13 incremental Switch build exits 0.

## Test Checklist
- [x] Established polling expires at the threshold and only once unless an explicit retry is enabled.
- [x] Poll/command refresh, explicit reset, owner loss/inactive playback, and sparse queries behave as specified.
- [x] Explicit Stop requests home once and supports retry without rearming the inactivity timer.

## Implementation Notes
Added a small atomic singleton because SOAP callbacks and the main/UI loop run on different threads. Four successful `GetTransportInfo`/`GetPositionInfo` calls within a 3-second rolling window arm tracking; successful polls and transport commands refresh the timestamp. Profile 13 consumes a one-shot timeout after 10 seconds only while DLNA owns active/loading/buffering/seeking/playing/paused media. A successful SOAP Stop publishes a separate immediate home event. Main consumes both events through the existing asynchronous return-home path and restores the event if submission fails. The focused target needed quotes around its output directory because the workspace path contains spaces. GCC 16 target compilation required standard static zero initialization instead of removed `ATOMIC_VAR_INIT`. A protective commit was intentionally skipped because the dirty worktree contains user/prior changes.

## Files Changed
- `makefile`
- `scripts/test_dlna_controller_session.c`
- `source/main.c`
- `source/protocol/dlna/control/action/avtransport.c`
- `source/protocol/dlna/control/controller_session.c`
- `source/protocol/dlna/control/controller_session.h`
