# Session Context

> Last Updated: 2026-07-21 01:17 NZST

## Current Task
修复首次 AirPlay 真机发现失败：保证本地上传构建包含 Ed25519，并为 integration/receiver/mDNS 启动提供可见且脱敏的分阶段诊断。

## Implementation State
- Steps 1-6 and 8-9 are completed with bounded protocol, identity/pairing, mDNS, H.264/AAC media, MPEG-TS and libmpv stream implementations.
- Steps 10-14 are implemented and covered by host/sanitizer/Switch builds; their remaining gates are physical iPhone/Switch playback and soak tests.
- Step 15 host tests, Docker build, strict Ed25519/libmpv/deko3d/ImGui build, package checks and continuous Release pipeline are automated and green.
- The fixed-source GPL PlayFair subset is integrated behind NX-Cast's bounded adapter; mirroring is advertised experimentally when the media callbacks are present.
- The composed receiver verifies all four mirror/video capability bits, a 142-byte stage-one reply, authorization failure isolation, and reconnect behavior.
- The first physical test did not reach discovery: nxlink uploaded a 25,391,802-byte local NRO built without `switch-libsodium`, so Ed25519 identity creation stopped the receiver before mDNS started.
- VS Code and direct nxlink builds now require AirPlay Ed25519, while opt-in AirPlay traces remain visible under the global WARN log threshold.

## Current Blockers
- Physical iPhone/Switch hardware is required for the 60-minute soak, ten reconnects, mixed DLNA/IPTV/AirPlay switching and shutdown matrix.
- This workstation lacks Switch libsodium, so the attested Ed25519 release build must run in the pinned CI image; the local development NRO build passes.
- A new physical discovery test must use the CI `continuous` NRO or a local strict build after installing `switch-libsodium`; the previously uploaded development NRO cannot provide AirPlay.

## CI State
- GitHub Actions build 96 first failed only in the continuous Release API with `Error creating policy` during a GitHub service incident; attempt 2 passed without source changes.
- CI no longer performs online `dkp-pacman -S` calls. The pinned official `devkita64` image already contains `switch-libsodium`; the Dockerfile verifies it locally with `dkp-pacman -Q`.
- `make test-airplay` includes deterministic unit/sanitizer coverage and real loopback RTSP, pairing, mDNS, receiver and HLS smoke tests.
- GitHub Actions build 98 passed the complete pipeline for capability/stack hardening commit `d1a4763`; artifact `8453509701` is 32,384,049 bytes.
- GitHub Actions run `29728903616` passed the complete pipeline for GPL PlayFair commit `f5e21b2`; artifact `8455447635` is 32,614,855 bytes and the continuous Release points to that commit.

## Next Actions
1. Install `switch-libsodium` locally or download the next successful `continuous` NRO, then rerun discovery with the AirPlay Trace launch configuration.
2. Run PIN pairing, RECORD/TEARDOWN and first-frame playback on a real iPhone/Switch with redacted traces.
3. Complete reconnect, mixed-protocol, shutdown and 60-minute A/V soak acceptance before claiming compatibility.
