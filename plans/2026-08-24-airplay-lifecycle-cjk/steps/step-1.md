# Step 1: Protocol-Scoped AirPlay Lifecycle

> Status: COMPLETED
> Created: 2026-08-24

## Goal
Remove peer-IP session aggregation and detached-stop timers while preserving explicit, stream-scoped stop behavior.

## Prerequisites
- Reference behavior verified in local UxPlay/RPiPlay sources.
- Files to modify: AirPlay logical session, receiver, integration, remote video, focused tests, and compatibility documentation.
- Design: protocol identity and explicit lifecycle events confirmed in the task overview.

## Deliverables
- RAOP remains connection/stream scoped and HTTP/reverse share only a validated Apple session identifier.
- Ordinary close releases transport state without scheduling active media stop.
- After this step: AirPlay session and remote-video tests pass with explicit lifecycle assertions.

## Plan
- [x] `edit` `scripts/test_airplay_session.c` and `scripts/test_airplay_remote_video.c` — replace peer/timer expectations with protective tests for independent RAOP and transport-only close.
- [x] `edit` `source/protocol/airplay/protocol/logical_session.[ch]` and `source/protocol/airplay/receiver.c` — remove peer association and generic TEARDOWN-to-URL coupling.
- [x] `edit` `source/protocol/airplay/media/remote_video.[ch]`, `source/protocol/airplay/integration.[ch]`, and `source/main.c` — remove grace polling while retaining pending negotiation cancellation and explicit `/stop` handling.
- [x] `edit` `docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md` and `docs/AIRPLAY_DEVELOPMENT.md` — document protocol-scoped lifecycle contracts.
- [x] `bash` `make test-airplay-session`, focused remote-video compile/run, and `make test-airplay` — all passed.

## Quality Checklist
- [x] Evidence-before-edit: targets read, callers found with `rg`, validation commands discovered in `makefile`.
- [x] Existing pattern / reuse checked: UxPlay Apple-session and typed-TEARDOWN behavior used as reference.
- [x] Contract understood: ordinary close affects transport only; explicit media commands affect playback.
- [x] Risk reviewed: lifecycle regression, stale owner, NAT collision, and cross-protocol ownership.
- [x] Mitigation recorded: focused black-box tests cover identity and stop boundaries.

## Validation Checklist
- [x] `make test-airplay-session` exits 0.
- [x] Focused remote-video compile/run exits 0 (the Makefile has no standalone phony target).
- [x] `make test-airplay` exits 0.
- [x] `git diff --check` exits 0 for touched files.

## Test Checklist
- [x] RAOP from the same peer is not rebound to an HTTP Apple session.
- [x] Final ordinary HTTP/reverse close does not stop active URL playback.
- [x] Explicit `/stop` still stops and releases ownership.

## Implementation Notes
Removed the topology-based identity fields and APIs, generic TEARDOWN forwarding, detach callback/deadline/poll path, and main-loop polling. A valid new `/play` is now the explicit replacement event, including after transport reconnect. Pending Reverse HLS is still cancelled when its final logical transport disappears. The worktree was already heavily dirty, so no WIP commit/stash was created because it would have mixed or hidden user changes.

## Files Changed
- `source/protocol/airplay/protocol/logical_session.h`
- `source/protocol/airplay/protocol/logical_session.c`
- `source/protocol/airplay/receiver.c`
- `source/protocol/airplay/media/remote_video.h`
- `source/protocol/airplay/media/remote_video.c`
- `source/protocol/airplay/integration.h`
- `source/protocol/airplay/integration.c`
- `source/main.c`
- `scripts/test_airplay_session.c`
- `scripts/test_airplay_remote_video.c`
- `docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md`
- `docs/AIRPLAY_DEVELOPMENT.md`
