# Session Context

> Last Updated: 2026-07-20 20:00 NZST

## Current Task
完成 NX-Cast AirPlay clean-room 实现的自动化与发布收尾，并准确记录无法在当前环境完成的真机/FairPlay 验收。

## Implementation State
- Steps 1-6 and 8-9 are completed with bounded protocol, identity/pairing, mDNS, H.264/AAC media, MPEG-TS and libmpv stream implementations.
- Steps 10-14 are implemented and covered by host/sanitizer/strict Switch builds, but their real-iPhone acceptance gates remain blocked by Step 7's standard FairPlay unwrap boundary.
- Step 15 host tests, Docker build, strict Ed25519/libmpv/deko3d/ImGui build, package checks and continuous Release pipeline are automated and green.
- Mirroring is intentionally not advertised; experimental non-DRM URL/HLS support remains the only AirPlay capability suitable for release claims.
- The composed receiver verifies that missing FairPlay unwrap support clears mirror/rotation capability bits, and the RTSP error path no longer overflows the 64 KiB Switch client-thread stack.

## Current Blockers
- A clean-room, legally usable implementation of the 142-byte FairPlay key unwrap boundary is unavailable, so a real iPhone cannot deliver usable standard mirror session keys.
- Physical iPhone/Switch hardware is required for the 60-minute soak, ten reconnects, mixed DLNA/IPTV/AirPlay switching and shutdown matrix.

## CI State
- GitHub Actions build 96 first failed only in the continuous Release API with `Error creating policy` during a GitHub service incident; attempt 2 passed without source changes.
- CI no longer performs online `dkp-pacman -S` calls. The pinned official `devkita64` image already contains `switch-libsodium`; the Dockerfile verifies it locally with `dkp-pacman -Q`.
- `make test-airplay` includes deterministic unit/sanitizer coverage and real loopback RTSP, pairing, mDNS, receiver and HLS smoke tests.
- GitHub Actions build 98 passed the complete pipeline for capability/stack hardening commit `d1a4763`; artifact `8453509701` is 32,384,049 bytes.

## Next Actions
1. Keep mirroring capability bits disabled until a lawful FairPlay-compatible boundary and real-device acceptance exist.
2. Run the documented iPhone/Switch hardware matrix when devices are available; collect only redacted AirPlay/media traces.
3. Reopen Steps 7 and 10-15 from BLOCKED only when the same real-device flow can be exercised end to end.
