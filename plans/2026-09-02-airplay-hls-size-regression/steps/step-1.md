# Step 1: Cover Aggregate Condensed Expansion

> Status: COMPLETED

## Goal
Allow bounded local HLS responses above 1 MiB while retaining the existing request limit.

## Prerequisites
- Latest trace and HLS/RTSP implementation inspected.

## Deliverables
- Aggregate expansion regression test.
- Separate request and response body limits.

## Plan
1. Add a condensed playlist fixture with many short URI lines and rewritten output above 1 MiB.
2. Run it red against the current implementation.
3. Introduce a bounded response limit and use it for HLS rewrite and response encoding.

## Quality Checklist
- [x] Request parsing remains at 1 MiB.
- [x] All arithmetic and allocations remain bounded.

## Validation Checklist
- [x] Focused HLS test passes.

## Test Checklist
- [x] Rewritten local playlist body exceeds 1 MiB.
- [x] Rewritten segment URLs are complete.
- [x] The complete HTTP response encodes above the request-size boundary.

## Implementation Notes
- Request bodies and request messages remain capped at 1 MiB; only bounded response bodies and response messages use the 4 MiB limit.
- No Bilibili lifecycle changes in this step.

## Files Changed
- `source/protocol/airplay/protocol/rtsp.h`
- `source/protocol/airplay/protocol/rtsp.c`
- `source/protocol/airplay/media/remote_hls.c`
- `scripts/test_airplay_remote_hls.c`
