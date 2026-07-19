# Session Context

> Last Updated: 2026-07-20 01:28 NZST

## Current Task
依照 AirPlay clean-room 开发计划，用 NX-Cast C 接口逐步实现 iPhone 到 Switch 的视频镜像和 URL/HLS 投送。

## Completed Steps
| Step | Summary | Files Changed |
|------|---------|---------------|
| Step 1 | Added a bounded, idempotent AirPlay lifecycle/status facade and host test target without enabling network behavior. | `makefile`, `source/protocol/airplay/airplay.[ch]`, `scripts/test_airplay.c`, plan files |
| Step 2 | Added a bounded clean-room binary plist value tree/codec, malformed fixtures and interoperability tests. | `makefile`, `source/protocol/airplay/protocol/plist.[ch]`, `scripts/test_airplay_plist.c`, fixtures, plan files |
| Step 3 | Added an independent persistent RTSP/HTTP server, bounded parser, session state and real TCP smoke tests. | `makefile`, `source/protocol/airplay/{server,protocol/rtsp}.[ch]`, RTSP test/smoke scripts, plan files |
| Step 4 | Added atomic persistent identity and bounded mbedTLS/libsodium primitives with published vectors. | `makefile`, `source/protocol/airplay/security/{crypto,identity}.[ch]`, crypto tests, plan files |
| Step 5 | Added Apple-compatible PIN Pair Setup/Verify, persisted trusted clients and TCP authorization/reconnect tests. | `makefile`, `source/protocol/airplay/security/{srp,pairing_store,pairing}.[ch]`, pairing tests/smoke, plan files |
| Step 6 | Added compressed DNS-SD records and a native mDNS responder with announce/query/conflict/goodbye tests. | `makefile`, `source/protocol/airplay/discovery/{dns,mdns}.[ch]`, DNS test and UDP smoke, plan files |

## Current Step
**Step 7: Control Handshake Integration** — Status: IN_PROGRESS
- Steps 1-6 normal tests, ASan/UBSan tests, static analysis, TCP/UDP smoke and strict Switch builds pass.
- Runtime/release AirPlay remains gated on official `switch-libsodium`; Step 15 CI must enforce `NXCAST_REQUIRE_AIRPLAY_ED25519=1`.

## Key Learnings
- `source/main.c` still has no AirPlay startup call, so the new lifecycle module has no runtime network side effects.
- The tracked build file is lowercase `makefile`; it already discovers C files directly under `source/protocol/airplay`.
- The lifecycle facade is application-thread-only until later transport steps add synchronization.
- The plist codec owns all decoded allocations, bounds untrusted input before traversal and keeps high-bit integer payloads as raw `uint64_t` bits.
- RTSP requests/responses live on each client worker's heap, socket closure has a single atomic owner, and disconnect callbacks provide the later secure-zero boundary.
- mbedTLS 2.28 supplies all planned primitives except Ed25519; the audited libsodium backend is capability-gated and must be mandatory before runtime AirPlay is enabled.
- Identity seeds remain private to `identity.c`; callers can obtain the public key/fingerprint and request signatures but cannot export the seed.
- Pair Setup/Verify authorizes the RTSP session and later media-key derivation; the selected legacy behavior does not add a general encrypted record layer around every later RTSP message.
- mDNS advertises only explicitly supplied feature bits; Step 6 uses legacy pairing only and defers screen/video/HLS bits to their accepted implementation phases.

## Next Actions
1. Inventory current `/server-info`, SETUP, RECORD, GET/SET_PARAMETER and TEARDOWN behavior from both references.
2. Add a runtime owner that loads pairing identity, starts RTSP, then mDNS, with reverse-order shutdown.
3. Add sanitized control transcripts, state validation and trace-safe lifecycle logging.
