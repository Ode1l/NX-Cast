# Session Context

> Last Updated: 2026-07-20 20:40 NZST

## Current Task
以 NX-Cast 自有协议/媒体框架接入来源固定、许可证完整的 GPL PlayFair 后端，解除 AirPlay 镜像 FairPlay 阻塞并继续真机前验收。

## Implementation State
- Steps 1-6 and 8-9 are completed with bounded protocol, identity/pairing, mDNS, H.264/AAC media, MPEG-TS and libmpv stream implementations.
- Steps 10-14 are implemented and covered by host/sanitizer/Switch builds; their remaining gates are physical iPhone/Switch playback and soak tests.
- Step 15 host tests, Docker build, strict Ed25519/libmpv/deko3d/ImGui build, package checks and continuous Release pipeline are automated and green.
- The fixed-source GPL PlayFair subset is integrated behind NX-Cast's bounded adapter; mirroring is advertised experimentally when the media callbacks are present.
- The composed receiver verifies all four mirror/video capability bits, a 142-byte stage-one reply, authorization failure isolation, and reconnect behavior.

## Current Blockers
- Physical iPhone/Switch hardware is required for the 60-minute soak, ten reconnects, mixed DLNA/IPTV/AirPlay switching and shutdown matrix.
- This workstation lacks Switch libsodium, so the attested Ed25519 release build must run in the pinned CI image; the local development NRO build passes.

## CI State
- GitHub Actions build 96 first failed only in the continuous Release API with `Error creating policy` during a GitHub service incident; attempt 2 passed without source changes.
- CI no longer performs online `dkp-pacman -S` calls. The pinned official `devkita64` image already contains `switch-libsodium`; the Dockerfile verifies it locally with `dkp-pacman -Q`.
- `make test-airplay` includes deterministic unit/sanitizer coverage and real loopback RTSP, pairing, mDNS, receiver and HLS smoke tests.
- GitHub Actions build 98 passed the complete pipeline for capability/stack hardening commit `d1a4763`; artifact `8453509701` is 32,384,049 bytes.

## Next Actions
1. Push the GPL PlayFair integration and require the complete Docker/release CI pipeline to pass.
2. Run discovery, PIN pairing, RECORD/TEARDOWN and first-frame playback on a real iPhone/Switch with redacted traces.
3. Complete reconnect, mixed-protocol, shutdown and 60-minute A/V soak acceptance before claiming compatibility.
