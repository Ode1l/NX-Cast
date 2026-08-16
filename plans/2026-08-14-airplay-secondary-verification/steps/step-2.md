# Step 2: Make Secondary Connection Diagnostics Explicit

> Status: COMPLETED
> Created: 2026-08-14

## Goal
Make the next Switch trace distinguish RAOP, secondary AirPlay, and malformed control requests without exposing identifiers or secrets.

## Prerequisites
- Step 1 completed with no-CSeq pair verification support.
- Files to modify: `source/protocol/airplay/server.c` and relevant lifecycle/smoke tests if required.
- Design: log only protocol and boolean header presence, never header values or pairing bodies.

## Deliverables
- Control request trace includes protocol, CSeq presence, and Apple session-ID presence.
- Existing server lifecycle and composed receiver behavior remain unchanged.
- After this step: the next real-device log identifies which secondary connection proceeds to video control.

## Plan
- [x] `read` `source/protocol/airplay/server.c` and `source/protocol/airplay/protocol/rtsp.h` — confirm safe header lookup and trace formatting.
- [x] `edit` `source/protocol/airplay/server.c` — add boolean-only connection classification fields to the control request trace.
- [x] `bash` `make test-airplay` — verify lifecycle, pairing, and receiver smoke behavior.

## Quality Checklist
- [x] Evidence-before-edit: target reads above, impact search `rg -n "control request" source scripts`, validation `make test-airplay`.
- [x] Existing pattern / reuse checked: reuse `airplay_rtsp_request_header()` and current trace macros.
- [x] Contract understood: diagnostics only; no routing or ownership side effect.
- [x] Risk reviewed: secret/identifier leakage and log volume.
- [x] Mitigation recorded: presence bits only, one line per control request under AirPlay trace.

## Validation Checklist
- [x] `make test-airplay` exits 0.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] Existing AirPlay server lifecycle and composed receiver smoke tests pass.

## Implementation Notes
The request trace now includes protocol plus boolean CSeq and `X-Apple-Session-ID` presence. No identifier value, pairing body, key, or signature is logged. The complete host suite, including server lifecycle and composed receiver smoke, passed.

## Files Changed
- `source/protocol/airplay/server.c`
