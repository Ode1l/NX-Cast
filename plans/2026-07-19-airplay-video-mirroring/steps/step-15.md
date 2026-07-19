# Step 15: Hardening and Release Readiness

> Status: BLOCKED
> Created: 2026-07-19

## Goal
完成 AirPlay 视频功能的兼容性、稳定性、安全、CI、文档和发布验收。

## Prerequisites
- Step 14 completed — all planned features and lifecycle integration work on Switch.
- At least two supported iPhone/iOS combinations or a documented single-device limitation are available.

## Deliverables
- CI 自动运行 AirPlay host tests、严格 Switch Docker build 和 release package checks。
- README/开发文档准确说明支持范围、配对方法、SD 文件、非 DRM 限制、故障诊断和参考实现边界。

## Plan
- [x] `rg` `.github/workflows`, Docker scripts, packaging and README paths — map existing release pipeline and assets.
- [x] `edit` `.github/workflows/` and Docker build scripts — run `make test-airplay` before strict Switch/release build.
- [x] `edit` `scripts/package_release.sh` and SD skeleton — package only the AirPlay privacy notice and reject runtime secrets/captures.
- [x] `write` `docs/AIRPLAY_DEVELOPMENT.md` — document architecture, phase support, test matrix, privacy/security and limitations.
- [x] `edit` `README.md`, changelog and release notes — document experimental URL/HLS without Apple certification or mirroring claims.
- [x] `bash` strict Docker build and release package checks — GitHub Actions run 29695349829 completed every build/package/release step successfully.
- [ ] `hardware` 60-minute/10-reconnect real-device acceptance — physical iPhone/Switch acceptance remains pending behind Step 7.

## Quality Checklist
- [x] Evidence-before-edit: current CI/package commands and generated SD contents recorded.
- [x] Existing pattern / reuse checked: current release workflow, README structure and trace tasks.
- [x] Contract understood: release advertises only URL/HLS bits and keeps mirroring disabled without FairPlay unwrap.
- [x] Risk reviewed: flaky network tests, secret fixtures, oversized package, licensing/branding and unsupported iOS claims.
- [x] Mitigation recorded: deterministic host fixtures, sanitized traces, sensitive-file package check and explicit limitations.

## Validation Checklist
- [x] All host tests and strict local/Docker Switch builds exit 0; the remote build requires libmpv, deko3d, ImGui and AirPlay Ed25519.
- [x] Remote release archive contains the 25,354,938-byte NRO, intact IPTV presets including `sources.txt`, AirPlay notice/licenses, and no keys, pairings, traces, dumps, captures or reference source.
- [x] Every unavailable real-device matrix cell is documented without claiming compatibility.

## Test Checklist
- [x] Automated regression covers IPTV navigation plus AirPlay RTSP, pairing, mDNS, composed receiver, mirror/video/audio, shutdown cancellation, reconnect, malformed input and direct HLS.
- [ ] Physical regression covers DLNA/IPTV/AirPlay switching, iPhone reconnect and long soak.

## Implementation Notes
- The Docker image installs native mbedTLS/libsodium/FFmpeg test dependencies and official devkitPro `switch-libsodium`. CI runs `make test-airplay` before a build that requires libmpv, deko3d, and Ed25519.
- Local Docker validation was not possible because the workstation has no `docker` command. GitHub Actions now provides the authoritative Docker execution evidence.
- The first remote Docker run proved image creation and dependency installation, then exposed Debian's lack of `mbedcrypto.pc`; host dependency detection now falls back to the installed system header/library instead of reporting mbedTLS missing.
- This workstation's Switch portlibs do not contain `switch-libsodium`; the new release gate correctly rejects `NXCAST_REQUIRE_AIRPLAY_ED25519=1`. The normal strict build still passes, and Docker/CI supplies the missing package.
- `make release-build` records a four-feature attestation only after libmpv, deko3d, ImGui and Ed25519 gates pass. Packaging rejects a missing attestation by default; the local layout-only test used the explicit non-release override because this workstation cannot produce an Ed25519 Switch build.
- Release staging includes `airplay/README.txt` but never runtime `identity.bin` or `pairings.bin`. It also rejects common private-key, log, trace, dump, and packet-capture suffixes.
- URL/HLS remains explicitly experimental. Mirroring remains unadvertised and unsupported; the two-device, ten-reconnect and 60-minute matrix requires physical hardware.
- GitHub Actions run `29695349829` passed host tests, strict Ed25519 Switch build, package generation, artifact upload and continuous Release update. The artifact is `nx-cast-04d0b98bb4c6f15442af69c362916cfe8cd76eb3` (32,384,206 bytes).
- The downloaded continuous SD archive is 19,647,201 bytes and contains a 25,354,938-byte NRO plus the three packaged IPTV source entries. Its SHA-256 is `74a3a2814c7dd92cec4ec310858d31efb0c44678f77cd6a2128a309fbd04f8cb`.

## Files Changed
- `Dockerfile`, `.github/workflows/build.yml`, `.github/workflows/release.yml`, `scripts/docker_build_release.sh`
- `scripts/package_release.sh`, `makefile`, `assets/airplay/README.txt`, `assets/licenses/LICENSE.libsodium.txt`
- `scripts/test_airplay_crypto.c`, `scripts/airplay_pairing_smoke_server.c`, `scripts/airplay_receiver_smoke_server.c`
- `source/protocol/airplay/discovery/mdns.c`, `source/protocol/airplay/media/stream_bridge.c`
- `README.md`, `docs/README.md`, `docs/install.md`, `docs/AIRPLAY_DEVELOPMENT.md`
- `.github/release-notes.md`, `CHANGELOG.md`, `third_party/NOTICE.md`
