# Session Context

> Last Updated: 2026-07-21 02:10 NZST

## Current Task
修复安装 `switch-libsodium` 后 AirPlay 构建在启动阶段立即闪退，并保留 integration/receiver/mDNS 的脱敏分阶段诊断。

## Implementation State
- Steps 1-6 and 8-9 are completed with bounded protocol, identity/pairing, mDNS, H.264/AAC media, MPEG-TS and libmpv stream implementations.
- Steps 10-14 are implemented and covered by host/sanitizer/Switch builds; their remaining gates are physical iPhone/Switch playback and soak tests.
- Step 15 host tests, Docker build, strict Ed25519/libmpv/deko3d/ImGui build, package checks and continuous Release pipeline are automated and green.
- The fixed-source GPL PlayFair subset is integrated behind NX-Cast's bounded adapter; mirroring is advertised experimentally when the media callbacks are present.
- The composed receiver verifies all four mirror/video capability bits, a 142-byte stage-one reply, authorization failure isolation, and reconnect behavior.
- The first physical test did not reach discovery: nxlink uploaded a 25,391,802-byte local NRO built without `switch-libsodium`, so Ed25519 identity creation stopped the receiver before mDNS started.
- VS Code and direct nxlink builds now require AirPlay Ed25519, while opt-in AirPlay traces remain visible under the global WARN log threshold.
- After `switch-libsodium` was installed, two 25,461,434-byte local uploads reset nxlink before any application trace. Static inspection showed the package's default sysrandom backend opens `/dev/urandom` and calls `sodium_misuse()` when that fails on libnx.
- NX-Cast now registers a Switch-only libsodium randombytes backend backed by `randomGet()` before the first `sodium_init()`. The strict traced NRO contains the expected call order and startup-stage markers.

## Current Blockers
- Physical iPhone/Switch hardware is required for the 60-minute soak, ten reconnects, mixed DLNA/IPTV/AirPlay switching and shutdown matrix.
- The corrected libsodium startup path still requires a physical upload test; static binary inspection and host/Switch builds cannot prove Horizon runtime behavior.
- Discovery, PIN pairing and media acceptance remain pending after startup survives with the corrected NRO.

## CI State
- GitHub Actions build 96 first failed only in the continuous Release API with `Error creating policy` during a GitHub service incident; attempt 2 passed without source changes.
- CI no longer performs online `dkp-pacman -S` calls. The pinned official `devkita64` image already contains `switch-libsodium`; the Dockerfile verifies it locally with `dkp-pacman -Q`.
- `make test-airplay` includes deterministic unit/sanitizer coverage and real loopback RTSP, pairing, mDNS, receiver and HLS smoke tests.
- GitHub Actions build 98 passed the complete pipeline for capability/stack hardening commit `d1a4763`; artifact `8453509701` is 32,384,049 bytes.
- GitHub Actions run `29728903616` passed the complete pipeline for GPL PlayFair commit `f5e21b2`; artifact `8455447635` is 32,614,855 bytes and the continuous Release points to that commit.
- GitHub Actions run `29745733797` passed the strict Ed25519/deko3d/ImGui pipeline for discovery-build fix `c6fd4ca`; artifact `8462340191` is 32,615,208 bytes and the 25,461,434-byte continuous NRO was downloaded into the workspace for the next upload-only test.
- The local corrected clean release build, SD package verification, normal host suite, ASan/UBSan suite and strict TRACE rebuild all pass; remote CI for this fix is pending push.

## Next Actions
1. Upload the corrected strict TRACE NRO and confirm startup reaches the `ed25519`, receiver and mDNS stage markers without resetting nxlink.
2. Run PIN pairing, RECORD/TEARDOWN and first-frame playback on a real iPhone/Switch with redacted traces.
3. Complete reconnect, mixed-protocol, shutdown and 60-minute A/V soak acceptance before claiming compatibility.
