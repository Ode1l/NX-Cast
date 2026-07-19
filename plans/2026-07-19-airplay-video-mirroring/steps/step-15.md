# Step 15: Hardening and Release Readiness

> Status: PENDING
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
- [ ] `rg` `.github/workflows`, Docker scripts, packaging and README paths — map existing release pipeline and assets.
- [ ] `edit` `.github/workflows/` and Docker build scripts — run `make test-airplay` before strict Switch/release build.
- [ ] `edit` `scripts/package_release.sh` and SD skeleton only if AirPlay runtime directories/defaults are required.
- [ ] `write` `docs/AIRPLAY_DEVELOPMENT.md` — document architecture, phase support, test matrix, privacy/security and limitations.
- [ ] `edit` `README.md` and release notes — describe iPhone video/mirroring support without Apple certification claims.
- [ ] `bash` full host suite, strict Docker build, package inspection and 60-minute/10-reconnect real-device acceptance.

## Quality Checklist
- [ ] Evidence-before-edit: current CI/package commands and generated SD contents recorded.
- [ ] Existing pattern / reuse checked: current release workflow, README structure and trace tasks.
- [ ] Contract understood: release advertises only capability bits and scenarios that passed the matrix.
- [ ] Risk reviewed: flaky network tests, secret fixtures, oversized package, licensing/branding and unsupported iOS claims.
- [ ] Mitigation recorded: deterministic host fixtures, sanitized traces, package manifest check and explicit limitations.

## Validation Checklist
- [ ] All host tests and strict local/Docker Switch builds exit 0.
- [ ] Release archive contains expected NRO/SD assets and no keys, traces, dumps or reference source.
- [ ] Real-device acceptance passes or every unsupported matrix cell is documented.

## Test Checklist
- [ ] Regression suite covers DLNA, IPTV, AirPlay mirror/video, shutdown, reconnect, malformed input and long soak.

## Implementation Notes
Pending.

## Files Changed
Pending.
