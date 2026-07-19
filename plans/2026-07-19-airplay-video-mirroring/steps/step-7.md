# Step 7: iPhone Control Handshake Milestone

> Status: BLOCKED
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
- Independent behavior review found that the 16-byte FairPlay phase depends on a proprietary 142-byte response table/algorithm and wrapped-key derivation. Copying those constants from UxPlay/RPiPlay would violate the clean-room decision; the default backend therefore returns 501/fails closed, while tests inject an audited-key-backend-shaped callback.
- Normal tests, ASan/UBSan, Clang static analysis, real TCP receiver/mDNS/pairing/transport smoke and strict Switch builds with trace disabled/enabled pass.
- Blocker: a real iPhone cannot reach RECORD until a legally and technically acceptable FairPlay compatibility backend or an independently specified algorithm is available. No mirroring feature bit is advertised by the default runtime.

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
