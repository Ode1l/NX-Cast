# Step 5: Pair Setup and Pair Verify

> Status: COMPLETED
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
- [x] `rg` both references for Pair Setup/Verify endpoint sequence and plist keys — produce a state/field inventory only.
- [x] `write` `source/protocol/airplay/security/pairing.h` and `.c` — implement state machine over Step 2/4 APIs.
- [x] `edit` `source/protocol/airplay/protocol/rtsp.c` — route pairing endpoints and enable encryption only after verification.
- [x] `edit` `source/protocol/airplay/airplay.h` — add PIN display/dismiss callbacks without UI-thread access.
- [x] `write` sanitized transcript fixtures and extend `scripts/test_airplay.c` for success, wrong PIN, replay and disconnect.
- [x] `bash` `make test-airplay` — expect transcript outputs and state transitions to match fixtures.

## Quality Checklist
- [x] Evidence-before-edit: transcript fields and ordering recorded from references/captures.
- [x] Existing pattern / reuse checked: plist, crypto and callback boundaries from Steps 1-4.
- [x] Contract understood: PIN is ephemeral; verified session owns derived keys until teardown.
- [x] Risk reviewed: secret logging, replay, invalid state, oracle errors and use-after-free on disconnect.
- [x] Mitigation recorded: generic auth failures, transcript tests, secure zero and single-owner session state.

## Validation Checklist
- [x] `make test-airplay` exits 0.
- [x] Protocol smoke rejects unauthenticated protected methods and survives reconnect.
- [x] Strict Switch build exits 0.

## Test Checklist
- [x] Deterministic pairing transcripts cover success, wrong PIN, malformed plist, replay and mid-handshake close.

## Implementation Notes
- Implemented the legacy Apple PIN flow used by the selected UxPlay behavior: `/pair-pin-start`, three `/pair-setup-pin` plist exchanges, then the two-message raw `/pair-verify` exchange.
- SRP uses the RFC 5054 2048-bit group with Apple's SHA-1 transcript and 40-byte interleaved session key. Fixed host vectors were independently generated with Python big integers.
- Verified clients are persisted in a bounded, checksummed `pairings.bin`; temporary files are flushed and renamed, and damaged stores are isolated as `.corrupt`.
- Pairing failure responses intentionally use one generic 470 path, close the connection, dismiss the PIN, and erase SRP/X25519 secrets. No PIN, proof, private key, shared secret, or decrypted payload is logged.
- The selected legacy mirroring behavior encrypts Pair Setup/Verify messages and derives later media keys, but does not wrap the remaining RTSP connection in a general encrypted record layer. Protected SETUP/RECORD methods remain unavailable until Pair Verify succeeds.
- Host tests use libsodium Ed25519 and mbedTLS 2.28 crypto. The ordinary Switch NRO builds with an unavailable Ed25519 stub on this machine; release/CI must install `switch-libsodium` and set `NXCAST_REQUIRE_AIRPLAY_ED25519=1` so a nonfunctional AirPlay package cannot be produced silently.
- Validation passed with normal tests, ASan/UBSan, Clang static analysis, real TCP reconnect smoke, and strict Switch builds for `TRACE_AIRPLAY=0` and `TRACE_AIRPLAY=1`.

## Files Changed
- `source/protocol/airplay/security/crypto.[ch]`
- `source/protocol/airplay/security/srp.[ch]`
- `source/protocol/airplay/security/pairing_store.[ch]`
- `source/protocol/airplay/security/pairing.[ch]`
- `source/protocol/airplay/protocol/rtsp.[ch]`
- `source/protocol/airplay/airplay.[ch]`
- `scripts/test_airplay_crypto.c`
- `scripts/test_airplay_srp.c`
- `scripts/test_airplay_pairing.c`
- `scripts/airplay_pairing_smoke_server.c`
- `scripts/smoke_airplay_pairing.py`
- `makefile`
