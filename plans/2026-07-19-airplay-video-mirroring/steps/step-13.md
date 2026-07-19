# Step 13: AirPlay URL and HLS Playback

> Status: PENDING
> Created: 2026-07-19

## Goal
接收 AirPlay 远程视频 URL/HLS 命令并复用 NX-Cast 现有播放器、进度和暂停控制。

## Prerequisites
- Step 12 completed — AirPlay session/security/lifecycle are stable.
- Files to inspect: `source/player/renderer.h`, DLNA AVTransport handlers, UxPlay remote-video endpoints.

## Deliverables
- `source/protocol/airplay/media/remote_video.[ch]` 实现 `/play`, `/scrub`, `/rate`, `/stop`, `/playback-info` 的状态映射。
- URL/HLS 直接交给 `renderer_set_uri()`/libmpv；不引入 HLS gateway/proxy，也不篡改发送方 Content-Type。

## Plan
- [ ] `read` `source/player/renderer.h` and DLNA transport actions — map existing thread-safe playback/query APIs.
- [ ] `rg` UxPlay current remote-video handlers and RPiPlay support gaps — define endpoint/body/status compatibility matrix.
- [ ] `write` `source/protocol/airplay/media/remote_video.[ch]` — validate URL, parse plist/text parameters and map commands.
- [ ] `edit` `source/protocol/airplay/protocol/handlers.c` — route remote-video endpoints and return playback state/time.
- [ ] `edit` `scripts/smoke_airplay.py` — test play/rate/scrub/info/stop sequences with a local HTTP/HLS fixture.
- [ ] `bash` host smoke and Switch tests from Safari/video apps — verify absolute and relative HLS segments via FFmpeg itself.

## Quality Checklist
- [ ] Evidence-before-edit: renderer API and AirPlay endpoint contracts documented.
- [ ] Existing pattern / reuse checked: DLNA AVTransport and IPTV direct URL playback.
- [ ] Contract understood: only http/https URLs accepted; player state owns authoritative position/duration.
- [ ] Risk reviewed: SSRF-like local URL access, malformed position, stale callbacks, app-specific headers and HLS redirects.
- [ ] Mitigation recorded: scheme/length validation, session generation, bounded metadata and local fixtures.

## Validation Checklist
- [ ] `make test-airplay` and remote-video smoke exit 0.
- [ ] Strict Switch build plays a direct MP4 and HLS URL controlled from an iPhone.
- [ ] Existing CCTV/iQiyi/DLNA/IPTV playback regression tests remain successful.

## Test Checklist
- [ ] Endpoint tests cover play, pause/rate, scrub, info, stop, invalid URL, redirects and relative HLS segments.

## Implementation Notes
Pending.

## Files Changed
Pending.
