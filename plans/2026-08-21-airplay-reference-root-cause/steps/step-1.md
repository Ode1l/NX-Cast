# Step 1: Compare Direct-Play Lifecycle

> Status: COMPLETED
> Created: 2026-08-21

## Goal
Determine whether Bilibili playback stops because of decoding, an explicit sender command, or incorrect coupling between connection and media lifetime.

## Prerequisites
- Latest available trace identified as `logs/run_nxlink-20260817-210916.log`.
- Reference sources available under `../others/UxPlay-master` and `../others/RPiPlay-master`.
- No protocol source edits are permitted in this analysis step.

## Deliverables
- A timestamped chain from `/play` through first frame to stop.
- A reference comparison showing which event is allowed to terminate direct URL playback.
- After this step: Bilibili's confirmed root cause and remaining uncertainty are documented.

## Plan
- [x] `rg` `logs/run_nxlink-20260817-210916.log` — correlate logical session, reverse connection, player sequence, first frame, and stop.
- [x] `read` `source/protocol/airplay/media/remote_video.c` and `source/protocol/airplay/integration.c` — trace final connection close into player ownership release.
- [x] `rg`/`read` `../others/UxPlay-master/lib/raop.c`, `lib/http_handlers.h`, and `uxplay.cpp` — compare generic connection destruction with explicit `/stop` and reset behavior.
- [x] `rg` `../others/RPiPlay-master/lib/raop.c` — ensure mirror/RAOP transport teardown is not incorrectly generalized to URL playback.

## Quality Checklist
- [x] Evidence-before-edit: target trace and lifecycle sources read; no implementation edit; validation is chronology comparison.
- [x] Existing pattern / reuse checked: UxPlay lifecycle callbacks and explicit stop handler inspected.
- [x] Contract understood: connection close, explicit stop, media end, and player release are separate side effects.
- [x] Risk reviewed: stale ownership, premature stop, and cross-protocol fallback.
- [x] Mitigation recorded: do not recommend code until reference behavior and trace agree.

## Validation Checklist
- [x] Every conclusion cites a trace line and a source path.
- [x] Sender-originated close is distinguished from NX-Cast-originated player Stop.

## Test Checklist
- [x] N/A — analysis step; no runtime behavior is modified.

## Implementation Notes
The Bilibili stream reached mpv and presented a hardware-decoded first frame. The sender then closed the AirPlay control and reverse sockets without sending `POST /stop`; NX-Cast converted the final logical close into a delayed owner release, and `integration_remote_release()` submitted `PLAYER_COMMAND_STOP`. UxPlay's generic `conn_destroy()` only updates connection-scoped/audio state, while HLS media stop/reset is driven by explicit `/stop`, EOS, replacement, or HLS shutdown. RPiPlay has no modern URL/reverse-HLS route and is not applicable to this lifecycle.

## Files Changed
- `plans/2026-08-21-airplay-reference-root-cause/plan.md`
- `plans/2026-08-21-airplay-reference-root-cause/steps/step-1.md`
