# Step 4: Device Identity and Crypto Primitives

> Status: PENDING
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
- [ ] `read` mbedTLS headers/pkg-config and `source/main.c` storage setup — verify available APIs and directory timing.
- [ ] `write` `source/protocol/airplay/security/identity.[ch]` — persist versioned identity under `sdmc:/switch/NX-Cast/airplay/`.
- [ ] `write` `source/protocol/airplay/security/crypto.[ch]` — expose narrow typed operations and secure-zero helpers.
- [ ] `edit` `Makefile` — add explicit mbedTLS compile/link detection for Switch and host tests.
- [ ] `edit` `scripts/test_airplay.c` — add published algorithm vectors, identity reload and corrupt-file recovery tests.
- [ ] `bash` `make test-airplay` and strict Switch build — expect deterministic vectors and no unresolved symbols.

## Quality Checklist
- [ ] Evidence-before-edit: mbedTLS feature availability verified from installed headers and pkg-config.
- [ ] Existing pattern / reuse checked: reuse NX-Cast SD root and error logging conventions.
- [ ] Contract understood: private keys never leave security module; identity writes are atomic/versioned.
- [ ] Risk reviewed: weak RNG, key disclosure, partial files, API/version mismatch and timing-sensitive compare.
- [ ] Mitigation recorded: platform entropy check, secure zero, constant-time compare and known vectors.

## Validation Checklist
- [ ] `make test-airplay` exits 0 with crypto vectors enabled.
- [ ] Strict Switch build exits 0 with mbedTLS linked explicitly.

## Test Checklist
- [ ] Known vectors, first-run identity, reload, corrupted identity and unwritable-path behavior pass.

## Implementation Notes
Pending.

## Files Changed
Pending.
