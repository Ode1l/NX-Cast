# Step 4: Device Identity and Crypto Primitives

> Status: COMPLETED
> Created: 2026-07-19

## Goal
使用 mbedTLS 建立持久 AirPlay 设备身份、随机数和配对/会话所需加密原语。

## Prerequisites
- Step 3 completed — control server exposes session-owned security context hooks.
- Files to inspect: `Makefile`, `/opt/devkitpro/portlibs/switch/lib/pkgconfig/mbedtls.pc`, existing SD storage setup.

## Deliverables
- `source/protocol/airplay/security/identity.[ch]` 原子创建/加载设备身份并处理损坏文件。
- `source/protocol/airplay/security/crypto.[ch]` 封装所需 hash、HKDF、Curve/Ed key、AES/ChaCha 操作且不暴露密钥日志。

## Plan
- [x] `read` mbedTLS headers/pkg-config and `source/main.c` storage setup — verified available APIs and directory timing.
- [x] `write` `source/protocol/airplay/security/identity.[ch]` — persisted versioned identity under `sdmc:/switch/NX-Cast/airplay/`.
- [x] `write` `source/protocol/airplay/security/crypto.[ch]` — exposed narrow typed operations and secure-zero helpers.
- [x] `edit` `makefile` — added explicit mbedTLS/libsodium compile and link detection for Switch and host tests.
- [x] `write` `scripts/test_airplay_crypto.c` — added published algorithm vectors, identity reload and corrupt-file recovery tests.
- [x] `bash` `make test-airplay` and strict Switch build — deterministic vectors pass with no unresolved symbols.

## Quality Checklist
- [x] Evidence-before-edit: mbedTLS feature availability verified from installed headers and pkg-config.
- [x] Existing pattern / reuse checked: reused NX-Cast SD root and atomic-file conventions.
- [x] Contract understood: private keys never leave security module; identity writes are atomic/versioned.
- [x] Risk reviewed: weak RNG, key disclosure, partial files, API/version mismatch and timing-sensitive compare.
- [x] Mitigation recorded: platform entropy check, secure zero, constant-time compare and known vectors.

## Validation Checklist
- [x] `make test-airplay` exits 0 with crypto vectors enabled.
- [x] Strict Switch build exits 0 with mbedTLS linked explicitly.

## Test Checklist
- [x] Known vectors, first-run identity, reload, corrupted identity and unwritable-path behavior pass.

## Implementation Notes
- Switch and host mbedTLS are both 2.28.10. It supplies entropy/CTR-DRBG, SHA-2, HMAC, HKDF, Curve25519, AES-CTR and ChaCha20-Poly1305.
- mbedTLS 2.28 does not implement Ed25519. The crypto facade uses audited libsodium for Ed25519 and exposes an explicit availability check; `NXCAST_REQUIRE_AIRPLAY_ED25519=1` prevents release builds from silently omitting it.
- The current local devkitPro prefix does not yet contain `switch-libsodium`, so the ordinary strict build validates the unavailable backend while host RFC 8032 vectors validate the implementation. Step 5/CI must install the official package and use the requirement gate.
- Identity records are 84-byte versioned files with a SHA-256 integrity checksum. Writes use mode 0600, `fsync`, atomic `rename` and best-effort directory sync; invalid records are moved to `.corrupt` before regeneration.
- X25519 operations pass CTR-DRBG into mbedTLS scalar multiplication for blinding and reject an all-zero shared secret.

## Files Changed
- `makefile`
- `source/protocol/airplay/security/crypto.[ch]`
- `source/protocol/airplay/security/identity.[ch]`
- `scripts/test_airplay_crypto.c`
- AirPlay plan/context files
