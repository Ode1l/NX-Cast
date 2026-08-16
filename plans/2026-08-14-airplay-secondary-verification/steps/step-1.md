# Step 1: Accept Secondary Pair Verification Without CSeq

> Status: COMPLETED
> Created: 2026-08-14

## Goal
Allow the observed iOS secondary pair-verify transcript through RTSP validation without weakening ordinary RTSP sequencing rules.

## Prerequisites
- Latest trace and dispatcher source identify the failure before the pairing route.
- Files to modify: `source/protocol/airplay/protocol/rtsp.c`, `scripts/test_airplay_rtsp.c`, `scripts/test_airplay_pairing.c`.
- Design: only `POST /pair-verify` receives the missing-CSeq exemption.

## Deliverables
- Focused dispatcher coverage proves pair-verify without CSeq is routed and ordinary requests remain rejected.
- Pairing coverage proves a registered client can complete a fresh no-CSeq verification handshake.
- After this step: focused and complete AirPlay host tests pass.

## Plan
- [x] `read` `source/protocol/airplay/protocol/rtsp.c`, `scripts/test_airplay_rtsp.c`, and `scripts/test_airplay_pairing.c` — confirm request validation and helper conventions.
- [x] `edit` `scripts/test_airplay_rtsp.c` and `scripts/test_airplay_pairing.c` — add protective failing coverage for no-CSeq secondary verification and strict rejection elsewhere.
- [x] `edit` `source/protocol/airplay/protocol/rtsp.c` — add the narrow pair-verify exemption.
- [x] `bash` `make test-airplay` — expect all host regressions to pass.

## Quality Checklist
- [x] Evidence-before-edit: target reads above, impact search `rg -n "has_cseq|pair-verify" source scripts`, validation `make test-airplay`.
- [x] Existing pattern / reuse checked: reuse current discovery exemption and pairing cryptographic validation.
- [x] Contract understood: missing-CSeq pair verification has no sequence echo; success still requires registered Ed25519 identity proof.
- [x] Risk reviewed: protocol relaxation and authorization bypass.
- [x] Mitigation recorded: exact method/URI exemption plus positive cryptographic and negative ordinary-request tests.

## Validation Checklist
- [x] `make test-airplay` exits 0.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] RTSP test covers routed no-CSeq `/pair-verify` and rejected no-CSeq SETUP.
- [x] Pairing test completes both verification phases without CSeq for a registered client.

## Implementation Notes
The new dispatcher test first failed at the exact observed 400 branch. The RTSP dispatcher now exempts only `POST /pair-verify` from the missing-CSeq rejection. A persisted registered client completes both cryptographic verification phases without CSeq and receives no CSeq response header; ordinary no-CSeq SETUP remains rejected. The complete `make test-airplay` suite passed.

## Files Changed
- `source/protocol/airplay/protocol/rtsp.c`
- `scripts/test_airplay_rtsp.c`
- `scripts/test_airplay_pairing.c`
