# Step 1: Implement the YouTube HTTP Compatibility Contract

> Status: COMPLETED
> Created: 2026-08-14

## Goal
Replace the observed YouTube 501 responses with bounded UxPlay-compatible responses while preserving mirror `/info` behavior.

## Prerequisites
- User approved the `/server-info`, `/fp-setup2`, and `/setProperty` compatibility approach.
- Files to modify: `source/protocol/airplay/protocol/handlers.c`, `source/protocol/airplay/protocol/rtsp.c`, `scripts/test_airplay_handlers.c`.
- Latest trace and UxPlay reference behavior have been read and compared.

## Deliverables
- Dedicated HLS `/server-info` XML plist response.
- Safe 421 `/fp-setup2` response and tolerant property acknowledgements.
- After this step: focused and complete AirPlay handler tests pass.

## Plan
- [x] `edit` `scripts/test_airplay_handlers.c` - add failing assertions for `/server-info`, `/fp-setup2`, known and unknown `/setProperty` requests, and unchanged `/info`.
- [x] `edit` `source/protocol/airplay/protocol/rtsp.c` - add the standard 421 reason phrase.
- [x] `edit` `source/protocol/airplay/protocol/handlers.c` - add bounded dedicated handlers and route them before the 501 fallback.
- [x] `bash` `make test-airplay` - expect all AirPlay and shared regression tests to pass.

## Quality Checklist
- [x] Evidence-before-edit: target reads complete; impact search `rg -n "server-info|fp-setup2|setProperty" source scripts`; validation `make test-airplay`.
- [x] Existing pattern / reuse checked: reuse response body/status helpers and existing handler dispatch test fixture.
- [x] Contract understood: compatibility endpoints have no playback mutation; only `/play` may later start media.
- [x] Risk reviewed: response format, body bounds, route ordering, and accidental FairPlay success claims.
- [x] Mitigation recorded: exact response assertions and separate `/info` regression coverage.

## Validation Checklist
- [x] `make test-airplay` exits 0.
- [x] `git diff --check` exits 0 for changed source and test files.

## Test Checklist
- [x] `/server-info`, `/fp-setup2`, known property, unknown property, and `/info` cases pass.

## Implementation Notes
The new transcript produced 13 expected failures against the old routing. The implementation now returns a dedicated XML `/server-info`, an explicit 421 without attempting unsupported decryption, XML `errorCode=0` for known properties, and an empty 200 for unknown properties. The complete host suite passes.

## Files Changed
- `source/protocol/airplay/protocol/handlers.c`
- `source/protocol/airplay/protocol/rtsp.c`
- `scripts/test_airplay_handlers.c`
