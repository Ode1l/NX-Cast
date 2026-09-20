# Step 1: Make FCUP Response Status Advisory

> Status: COMPLETED
> Created: 2026-08-17

## Goal
Allow valid `unhandledURLResponse` FCUP payloads to proceed even when `FCUP_Response_StatusCode` is missing, zero, or non-2xx.

## Prerequisites
- Plan created and approved.
- Files to modify: `source/protocol/airplay/media/remote_hls.c`, `scripts/test_airplay_remote_hls.c`.

## Deliverables
- Status parsing is diagnostic-only.
- Existing request-id, URL, session, and playlist validation remains unchanged.
- A focused test proves a `404` status still accepts the matching FCUP response.

## Plan
- [ ] `read` source/protocol/airplay/media/remote_hls.c — inspect status validation and cleanup path.
- [ ] `rg` `AIRPLAY_REMOTE_HLS_ACTION_RESULT_BAD_STATUS` — find enum/result-name callers.
- [ ] `edit` source/protocol/airplay/media/remote_hls.c — parse/log status without rejecting non-2xx values.
- [ ] `edit` scripts/test_airplay_remote_hls.c — change status test to expect success for `404`.
- [ ] `bash` make test-airplay — expect exit 0.

## Quality Checklist
- [ ] Evidence-before-edit: target and test read; impact search `AIRPLAY_REMOTE_HLS_ACTION_RESULT_BAD_STATUS`; validation `make test-airplay`.
- [ ] Existing pattern / reuse checked: keep `hls_get_uint` helper and bounded plist access.
- [ ] Contract understood: status is advisory; request/URL/playlist fields remain mandatory.
- [ ] Risk reviewed: protocol compatibility / correctness.
- [ ] Mitigation recorded: updated regression test and host suite.

## Validation Checklist
- [ ] `git diff --check` exits 0

## Test Checklist
- [ ] `make test-airplay` — all pass

## Implementation Notes
- Kept `FCUP_Response_StatusCode` parsing, but the value is now only traced as advisory.
- Updated the `404` fixture to expect `REQUEST_NEXT` because its `segment.m3u8` is a valid child playlist. The test also verifies the generated follow-up event.

## Files Changed
- `source/protocol/airplay/media/remote_hls.c`
- `scripts/test_airplay_remote_hls.c`
