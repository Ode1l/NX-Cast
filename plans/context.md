# Session Context

> Last Updated: 2026-07-20 00:53 NZST

## Current Task
依照 AirPlay clean-room 开发计划，用 NX-Cast C 接口逐步实现 iPhone 到 Switch 的视频镜像和 URL/HLS 投送。

## Completed Steps
| Step | Summary | Files Changed |
|------|---------|---------------|
| Step 1 | Added a bounded, idempotent AirPlay lifecycle/status facade and host test target without enabling network behavior. | `makefile`, `source/protocol/airplay/airplay.[ch]`, `scripts/test_airplay.c`, plan files |
| Step 2 | Added a bounded clean-room binary plist value tree/codec, malformed fixtures and interoperability tests. | `makefile`, `source/protocol/airplay/protocol/plist.[ch]`, `scripts/test_airplay_plist.c`, fixtures, plan files |
| Step 3 | Added an independent persistent RTSP/HTTP server, bounded parser, session state and real TCP smoke tests. | `makefile`, `source/protocol/airplay/{server,protocol/rtsp}.[ch]`, RTSP test/smoke scripts, plan files |

## Current Step
**Step 4: Device Identity and Crypto Primitives** — Status: PENDING
- Steps 1-3 normal tests, ASan/UBSan tests, static analysis and strict Switch builds pass.
- Step 4 will add versioned SD identity persistence and narrow mbedTLS wrappers without enabling the control listener yet.

## Key Learnings
- `source/main.c` still has no AirPlay startup call, so the new lifecycle module has no runtime network side effects.
- The tracked build file is lowercase `makefile`; it already discovers C files directly under `source/protocol/airplay`.
- The lifecycle facade is application-thread-only until later transport steps add synchronization.
- The plist codec owns all decoded allocations, bounds untrusted input before traversal and keeps high-bit integer payloads as raw `uint64_t` bits.
- RTSP requests/responses live on each client worker's heap, socket closure has a single atomic owner, and disconnect callbacks provide the later secure-zero boundary.

## Next Actions
1. Verify installed host/Switch mbedTLS feature and pkg-config availability plus the existing SD directory lifecycle.
2. Implement versioned atomic identity storage and narrow random/hash/HKDF/cipher/key wrappers with secure cleanup.
3. Add known vectors, identity recovery tests, sanitizer coverage and a strict Switch build before completing Step 4.
