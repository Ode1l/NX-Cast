# Step 2: Observable HLS Action Failures

> Status: COMPLETED
> Created: 2026-08-16

## Goal
Replace the boolean `/action` failure result with an explicit reason enum and focused tests without changing current HTTP status behavior.

## Prerequisites
- Step 1 completed.
- Files to modify: `source/protocol/airplay/media/remote_hls.h`, `source/protocol/airplay/media/remote_hls.c`, `source/protocol/airplay/media/remote_video.c`, `scripts/test_airplay_remote_hls.c`.

## Deliverables
- `AirPlayRemoteHlsActionResult` identifies each rejection branch.
- `handle_action` logs a secret-free reason while still returning HTTP 400 for invalid real-device payloads.
- Host tests assert representative success and failure reason codes.

## Plan
- [x] `edit` source/protocol/airplay/media/remote_hls.h — add result enum and reason-name accessor.
- [x] `edit` source/protocol/airplay/media/remote_hls.c — add result output to `airplay_remote_hls_handle_action` and set a distinct reason before every cleanup path.
- [x] `edit` source/protocol/airplay/media/remote_video.c — pass the result into `handle_action` and emit one `AIRPLAY_TRACE` reason line; keep existing 400 mapping.
- [x] `edit` scripts/test_airplay_remote_hls.c — update calls and add bad-status, bad-request-id, malformed-playlist, and URL-mismatch assertions.
- [x] `bash` make test-airplay — expect exit 0.

## Quality Checklist
- [x] Evidence-before-edit: target files re-read; impact search `rg airplay_remote_hls_handle_action`; validation `make test-airplay`.
- [x] Existing pattern / reuse checked: result-name helpers already used by nearby enums.
- [x] Contract understood: HTTP behavior preserved; only internal return shape and diagnostics change.
- [x] Risk reviewed: correctness / API / security / observability.
- [x] Mitigation recorded: tests pin failure reasons; logs exclude sessions, URLs, and bodies.

## Validation Checklist
- [x] `git diff --check` exits 0

## Test Checklist
- [x] `make test-airplay` — all pass

## Implementation Notes
Added `AirPlayRemoteHlsActionResult` with a named result for each failure boundary. The public function now returns the reason through an output parameter; callers and tests were updated together. Logging emits only session, body length, and reason name.

## Files Changed
- `source/protocol/airplay/media/remote_hls.h`
- `source/protocol/airplay/media/remote_hls.c`
- `source/protocol/airplay/media/remote_video.c`
- `scripts/test_airplay_remote_hls.c`
