# Step 7: iPhone Control Handshake Milestone

> Status: PENDING
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
- [ ] `rg` UxPlay first and RPiPlay second for `/info`, `/fp-setup`, SETUP/RECORD/TEARDOWN state/field differences — create a behavior matrix.
- [ ] `write` `source/protocol/airplay/protocol/handlers.[ch]` — implement endpoints with capability-gated fields and explicit session errors.
- [ ] `write` `source/protocol/airplay/security/fairplay.[ch]` — isolate required session-key negotiation from pairing and media code.
- [ ] `edit` `Makefile` and logging configuration — add opt-in `TRACE_AIRPLAY` with mandatory secret redaction.
- [ ] `edit` `scripts/smoke_airplay.py` and fixtures — replay sanitized control transcript through teardown.
- [ ] `bash` tests/build, then capture iPhone state/length/sequence trace — accept only a clean RECORD-to-TEARDOWN flow.

## Quality Checklist
- [ ] Evidence-before-edit: endpoint matrix distinguishes current UxPlay behavior from RPiPlay legacy behavior.
- [ ] Existing pattern / reuse checked: Step 2 plist, Step 3 RTSP, Step 5 pairing and existing trace flags.
- [ ] Contract understood: handler order is session-bound; unsupported methods return protocol errors without crashing.
- [ ] Risk reviewed: undocumented compatibility, proprietary handshake boundary, secret leakage and false success.
- [ ] Mitigation recorded: isolated module, deterministic transcripts, real-device gate and explicit block condition.

## Validation Checklist
- [ ] `make test-airplay` and local control smoke exit 0.
- [ ] Strict Switch build exits 0 with `TRACE_AIRPLAY=0` and `=1`.
- [ ] iPhone trace reaches RECORD and TEARDOWN without retry loop or leaked secret data.

## Test Checklist
- [ ] Host transcript and real-device matrix cover first pair, reconnect, wrong PIN, cancel and teardown.

## Implementation Notes
Pending.

## Files Changed
Pending.
