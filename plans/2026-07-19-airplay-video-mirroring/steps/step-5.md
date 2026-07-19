# Step 5: Pair Setup and Pair Verify

> Status: PENDING
> Created: 2026-07-19

## Goal
实现 PIN Pair Setup、Pair Verify 和后续控制通道加密状态机。

## Prerequisites
- Step 4 completed — identity and crypto wrappers pass known-vector tests.
- Files to inspect: UxPlay current pairing transcripts, RPiPlay legacy sequence, sanitized iPhone request captures.

## Deliverables
- `source/protocol/airplay/security/pairing.[ch]` 提供显式状态转换、PIN 回调和会话密钥生命周期。
- RTSP server 能拒绝越序/重放/认证失败消息，并在断开时清除临时密钥。

## Plan
- [ ] `rg` both references for Pair Setup/Verify endpoint sequence and plist keys — produce a state/field inventory only.
- [ ] `write` `source/protocol/airplay/security/pairing.h` and `.c` — implement state machine over Step 2/4 APIs.
- [ ] `edit` `source/protocol/airplay/protocol/rtsp.c` — route pairing endpoints and enable encryption only after verification.
- [ ] `edit` `source/protocol/airplay/airplay.h` — add PIN display/dismiss callbacks without UI-thread access.
- [ ] `write` sanitized transcript fixtures and extend `scripts/test_airplay.c` for success, wrong PIN, replay and disconnect.
- [ ] `bash` `make test-airplay` — expect transcript outputs and state transitions to match fixtures.

## Quality Checklist
- [ ] Evidence-before-edit: transcript fields and ordering recorded from references/captures.
- [ ] Existing pattern / reuse checked: plist, crypto and callback boundaries from Steps 1-4.
- [ ] Contract understood: PIN is ephemeral; verified session owns derived keys until teardown.
- [ ] Risk reviewed: secret logging, replay, invalid state, oracle errors and use-after-free on disconnect.
- [ ] Mitigation recorded: generic auth failures, transcript tests, secure zero and single-owner session state.

## Validation Checklist
- [ ] `make test-airplay` exits 0.
- [ ] Protocol smoke rejects unauthenticated protected methods and survives reconnect.
- [ ] Strict Switch build exits 0.

## Test Checklist
- [ ] Deterministic pairing transcripts cover success, wrong PIN, malformed plist, replay and mid-handshake close.

## Implementation Notes
Pending.

## Files Changed
Pending.
