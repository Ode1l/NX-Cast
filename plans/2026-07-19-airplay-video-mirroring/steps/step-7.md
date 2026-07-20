# Step 7: iPhone Control Handshake Milestone

> Status: IN_PROGRESS
> Created: 2026-07-19

## Goal
让真实 iPhone 从发现、配对进入 `/info`、`SETUP`、`RECORD` 和 `TEARDOWN` 完整控制会话，但暂不播放媒体。

## Prerequisites
- Step 6 completed — iPhone can discover NX-Cast and open the advertised control port.
- Sanitized `TRACE_AIRPLAY=1` logging policy is defined; no keys/PIN/body dumps are allowed.

## Deliverables
- `source/protocol/airplay/protocol/handlers.[ch]` 返回当前阶段所需设备信息和会话响应。
- `source/protocol/airplay/security/fairplay.[ch]` 仅实现镜像会话建立所需的非内容 DRM 兼容握手；若无法独立验证则停止并记录阻塞证据。

## Plan
- [x] `rg` UxPlay first and RPiPlay second for `/info`, `/fp-setup`, SETUP/RECORD/TEARDOWN state/field differences — create a behavior matrix.
- [x] `write` `source/protocol/airplay/protocol/handlers.[ch]` — implement endpoints with capability-gated fields and explicit session errors.
- [x] `write` `source/protocol/airplay/security/fairplay.[ch]` — isolate required session-key negotiation from pairing and media code.
- [x] `edit` `Makefile` and logging configuration — add opt-in `TRACE_AIRPLAY` with mandatory secret redaction.
- [x] `edit` `scripts/smoke_airplay.py` and fixtures — replay sanitized control transcript through teardown.
- [x] `vendor` GPL PlayFair from a fixed UxPlay commit — preserve source, copyright, license and provenance without importing unrelated server/media code.
- [x] `edit` `source/protocol/airplay/security/fairplay.[ch]` — adapt stage-one replies and wrapped-key decrypt through the isolated backend with bounded inputs and secure teardown.
- [x] `test` deterministic FairPlay fixtures, malformed modes, state ordering and receiver capability advertisement with the real backend.
- [ ] `bash` tests/build, then capture iPhone state/length/sequence trace — accept only a clean RECORD-to-TEARDOWN flow.

## Quality Checklist
- [x] Evidence-before-edit: endpoint matrix distinguishes current UxPlay behavior from RPiPlay legacy behavior.
- [x] Existing pattern / reuse checked: Step 2 plist, Step 3 RTSP, Step 5 pairing and existing trace flags.
- [x] Contract understood: handler order is session-bound; unsupported methods return protocol errors without crashing.
- [x] Risk reviewed: undocumented compatibility, proprietary handshake boundary, secret leakage and false success.
- [x] Mitigation recorded: isolated module, deterministic transcripts, real-device gate and explicit block condition.

## Validation Checklist
- [x] `make test-airplay` and local control smoke exit 0.
- [x] Strict Switch build exits 0 with `TRACE_AIRPLAY=0` and `=1`.
- [ ] iPhone trace reaches RECORD and TEARDOWN without retry loop or leaked secret data.

## Test Checklist
- [ ] Host transcript and real-device matrix cover first pair, reconnect, wrong PIN, cancel and teardown. Host transcripts pass; the real-device matrix is blocked.

## Implementation Notes
- `/info`, `/server-info`, `/fp-setup`, OPTIONS, SETUP, RECORD, FLUSH, parameter and TEARDOWN handling now use explicit per-connection state and never call the player/UI directly.
- The composed receiver owns pairing identity, TXT data, persistent control transport, lifecycle and optional mDNS in a deterministic start/stop order. Capability bits are removed when their callbacks are absent.
- The user-approved research route vendors only UxPlay's GPL PlayFair subset at commit `3ca7526387e894d6848b84c209de361c3bedd1ec`; the rest of the protocol, pairing, media, player, and UI remain NX-Cast implementations.
- Stage-one modes, stage-two ordering/mode bounds, response capacity, secure state reset, wrapped-key output, and receiver capability publication have deterministic host coverage. Invalid mode input is rejected before entering the legacy algorithm.
- Normal tests, ASan/UBSan, Clang static analysis, real TCP receiver/mDNS/pairing/transport smoke and strict Switch builds with trace disabled/enabled pass.
- Remaining gate: automated compatibility and Switch development builds pass, but a real iPhone/Switch must still prove discovery through RECORD/TEARDOWN and H.264/AAC playback before the feature can be called compatible.
- The user selected the GPL open-source research route. Integration must identify UxPlay and PlayFair as upstream sources, retain GPL notices, and must not describe the imported algorithm as clean-room or Apple-authorized.

## Files Changed
- `makefile`
- `source/protocol/airplay/trace.h`
- `source/protocol/airplay/protocol/rtsp.h`
- `source/protocol/airplay/protocol/handlers.[ch]`
- `source/protocol/airplay/security/fairplay.[ch]`
- `source/protocol/airplay/security/pairing.[ch]`
- `source/protocol/airplay/discovery/mdns.[ch]`
- `source/protocol/airplay/receiver.[ch]`
- `scripts/test_airplay_handlers.c`
- `scripts/airplay_receiver_smoke_server.c`
- `scripts/smoke_airplay.py`
- `scripts/smoke_airplay_receiver.py`
- `scripts/test_airplay_fairplay.c`
- `third_party/playfair/`
