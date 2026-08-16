# Plan: Restore AirPlay Secondary Verification

> Status: COMPLETED
> Created: 2026-08-14
> Last Updated: 2026-08-14

<!--
  Plan-level status (lifecycle):
    DRAFT     — awaiting approval after clarification
    ACTIVE    — execution in progress
    COMPLETED — all steps done, verified
    ARCHIVED  — optional long-term archival state
  This is distinct from step-level status (PENDING|IN_PROGRESS|COMPLETED|BLOCKED)
  in `steps/step-N.md`. The pre-edit gate checks step status, not plan status.
-->

## Goal
Allow current iOS clients to verify their secondary AirPlay control connection and continue to video negotiation instead of immediately tearing down the session.

## Assumptions
- The latest trace contains both a video-app AirPlay attempt and a Control Center attempt from the same paired iPhone.
- The no-CSeq `/pair-verify` connections are secondary AirPlay control connections, not malformed unrelated traffic.
- DLNA/IPTV and the shared player pipeline remain out of scope because both passed the same real-device run.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] An RTSP `POST /pair-verify` without `CSeq` reaches the pairing handler, while unrelated RTSP requests without `CSeq` remain rejected.
- [x] A registered client can complete both pair-verify messages without `CSeq` on a fresh secondary connection.
- [x] New trace output identifies protocol, CSeq presence, and Apple session-header presence without logging secrets.
- [x] AirPlay host tests and a strict Switch trace build pass without DLNA/IPTV source changes.

### Non-goals
- Implementing standalone AirPlay music, changing FFmpeg/deko3d, or claiming real-device video before the next trace.
- Broadly permitting every RTSP request without `CSeq`.

### Edge Cases
- Invalid or unregistered pair-verify bodies still fail inside the pairing handler.
- Missing `CSeq` remains a 400 for SETUP, RECORD, and ordinary RTSP control requests.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Missing-CSeq compatibility scope | Permit all RTSP requests, require an Apple session header, or exempt only pair verification | Exempt only `POST /pair-verify`; cryptographic pairing validation remains authoritative | yes — follows the observed iOS transcript and minimizes protocol relaxation |
| Diagnosis boundary | Modify media/FFmpeg or repair the pre-media secondary control connection | Repair secondary verification first | yes — latest trace has no `/play`, type-110 SETUP, mirror TCP client, or bridge media |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add focused missing-CSeq compatibility and pairing regression coverage. |
| Step 2 | `steps/step-2.md` | COMPLETED | Improve safe control-connection diagnostics and verify the composed receiver. |
| Step 3 | `steps/step-3.md` | COMPLETED | Run complete regression and strict Switch build validation. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| RTSP/pairing regression | `make test-airplay` | `makefile:648-688` | yes |
| Discovery regression | `python3 scripts/smoke_airplay_mdns.py` | `scripts/smoke_airplay_mdns.py` | yes |
| Switch build | `source /opt/devkitpro/switchvars.sh && make PORTLIBS_PREFIX=<staged-portlibs> TRACE_MEDIA=1 TRACE_INPUT=1 TRACE_AIRPLAY=1 NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 NXCAST_REQUIRE_AIRPLAY_ED25519=1 NXCAST_REQUIRE_AIRPLAY_MUXER=1 -j4` | `makefile`, previous successful strict build | yes |
| Diff hygiene | `git diff --check` | Git | yes |

## Context & Learnings
### Key Decisions
- Treat `request=0 status=400 headers=0 close=1` as an RTSP dispatcher rejection before the pairing route, not a cryptographic pairing failure.
- Keep authorization per TCP connection; the secondary connection performs its own pair-verify and can then carry `/play` or further video control.
### Gotchas & Warnings
- `Connection reset by peer` at application shutdown remains unrelated and must not be used as the diagnosis.
- The worktree contains ongoing uncommitted AirPlay work; preserve all unrelated changes.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260814-010000.log` | Current real-device transcript | `rg`/bounded `sed` show successful primary verification, audio-only setup, no video setup, repeated secondary `/pair-verify` status 400 at request count 0, then client TEARDOWN |
| `source/protocol/airplay/protocol/rtsp.c` | Pre-route RTSP validation | `read` shows all RTSP requests without CSeq except discovery GET are rejected before request count increments |
| `source/protocol/airplay/security/pairing.c` | Cryptographic secondary verification | `read` shows each TCP session owns an independent pairing context and pair-verify supports a fresh registered client |
| `source/protocol/airplay/server.c` | Control transcript diagnostics | `read` shows current trace omits protocol and CSeq/header classification |
| `scripts/test_airplay_rtsp.c` | Dispatcher contract tests | `rg` identifies the focused RTSP validation suite |
| `scripts/test_airplay_pairing.c` | Registered-client pair-verify tests | `read` shows reconnect verification exists but always supplies CSeq |
| `/tmp/uxplay-reference.vjph6D/lib/raop.c` | Read-only compatibility reference | `read` confirms UxPlay allows no-CSeq AirPlay connections when HLS is enabled and routes RTSP `/pair-verify` |
### Verified Facts
- All primary pair verification, FairPlay, initial SETUP, RECORD, timing, and optional audio SETUP responses are 200 in the latest run — verified by `logs/run_nxlink-20260814-010000.log`, 2026-08-14.
- No attempt reaches `/play`, type-110 mirror SETUP, mirror TCP acceptance, or bridge media; iPhone sends TEARDOWN itself — verified by bounded log search, 2026-08-14.
- Secondary `/pair-verify` receives 400 with `request=0`, no response headers, and no pairing-handler trace, exactly matching the RTSP missing-CSeq pre-route branch — verified by log/code comparison, 2026-08-14.
- DLNA and IPTV both play successfully in the same binary/run, so shared media/network resources are not the current failure boundary — verified by user report and current trace, 2026-08-14.
- A registered client now completes both pair-verify phases without CSeq while no-CSeq SETUP remains rejected — verified by `make test-airplay`, 2026-08-14.
- Control request traces now classify protocol, CSeq presence, and Apple session-header presence without exposing values — verified by source review and complete host compilation/tests, 2026-08-14.
- Complete AirPlay tests, mDNS smoke, strict Switch trace build, and diff hygiene pass; final NRO SHA-256 is `42356384c22fb42f92f0f5e8f8acb508194a676c47846923a3b40ec79a9858b3` — verified locally, 2026-08-14.
- Real-device video remains unverified until the next trace shows successful secondary verification followed by `/play` or type-110 SETUP — explicitly retained as the next-run contract, 2026-08-14.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-14 | Step 1 | Added a narrow no-CSeq exception for secondary pair verification with dispatcher, cryptographic reconnect, and strict negative regression tests. |
| 2026-08-14 | Step 2 | Added boolean-only secondary connection diagnostics; all AirPlay lifecycle and composed receiver tests pass. |
| 2026-08-14 | Step 3 | Re-ran complete regressions and produced a strict AirPlay trace NRO with recorded timestamp, size, hash, and next-run markers. |
| 2026-08-14 | Reflection | Re-read the plan, all step records, and every task-modified source/test file. The endpoint-specific compatibility rule preserves cryptographic authorization and ordinary RTSP strictness, diagnostics expose no secret values, all validations pass, and real-device continuation remains explicitly unclaimed until the next trace. |
