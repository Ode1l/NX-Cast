# Session Context

> Last Updated: 2026-07-20 00:01 NZST

## Current Task
依照 AirPlay clean-room 开发计划，用 NX-Cast C 接口逐步实现 iPhone 到 Switch 的视频镜像和 URL/HLS 投送。

## Completed Steps
| Step | Summary | Files Changed |
|------|---------|---------------|
| Step 1 | Added a bounded, idempotent AirPlay lifecycle/status facade and host test target without enabling network behavior. | `makefile`, `source/protocol/airplay/airplay.[ch]`, `scripts/test_airplay.c`, plan files |

## Current Step
**Step 2: Minimal Binary Plist Codec** — Status: PENDING
- Step 1 normal tests, ASan/UBSan tests and strict Switch build all pass.
- Binary plist will be independently implemented for only the value types and endpoints NX-Cast needs; libplist will not be introduced.

## Key Learnings
- `source/main.c` still has no AirPlay startup call, so the new lifecycle module has no runtime network side effects.
- The tracked build file is lowercase `makefile`; it already discovers C files directly under `source/protocol/airplay`.
- The lifecycle facade is application-thread-only until later transport steps add synchronization.

## Next Actions
1. Mark Step 2 `IN_PROGRESS` and inventory actual plist keys/types from the reference behavior.
2. Add bounded binary plist value ownership, decode/encode and malformed fixture tests.
3. Run `make test-airplay`, sanitizer tests and the strict Switch build before completing Step 2.
