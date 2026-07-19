# Session Context

> Last Updated: 2026-07-20 00:33 NZST

## Current Task
依照 AirPlay clean-room 开发计划，用 NX-Cast C 接口逐步实现 iPhone 到 Switch 的视频镜像和 URL/HLS 投送。

## Completed Steps
| Step | Summary | Files Changed |
|------|---------|---------------|
| Step 1 | Added a bounded, idempotent AirPlay lifecycle/status facade and host test target without enabling network behavior. | `makefile`, `source/protocol/airplay/airplay.[ch]`, `scripts/test_airplay.c`, plan files |
| Step 2 | Added a bounded clean-room binary plist value tree/codec, malformed fixtures and interoperability tests. | `makefile`, `source/protocol/airplay/protocol/plist.[ch]`, `scripts/test_airplay_plist.c`, fixtures, plan files |

## Current Step
**Step 3: Persistent RTSP/HTTP Control Server** — Status: PENDING
- Steps 1-2 normal tests, ASan/UBSan tests and strict Switch builds pass.
- Step 3 will add a standalone persistent AirPlay control transport; it must not change the DLNA server's one-request/close behavior.

## Key Learnings
- `source/main.c` still has no AirPlay startup call, so the new lifecycle module has no runtime network side effects.
- The tracked build file is lowercase `makefile`; it already discovers C files directly under `source/protocol/airplay`.
- The lifecycle facade is application-thread-only until later transport steps add synchronization.
- The plist codec owns all decoded allocations, bounds untrusted input before traversal and keeps high-bit integer payloads as raw `uint64_t` bits.

## Next Actions
1. Read Step 3 and inventory persistent RTSP/HTTP request/response shapes and connection lifecycle from the reference behavior.
2. Define a bounded parser and server ownership/stop contract without starting AirPlay from `main.c` yet.
3. Add host transcript tests, sanitizer coverage and a strict Switch build before completing Step 3.
