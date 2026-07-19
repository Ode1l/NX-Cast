# Step 13: AirPlay URL and HLS Playback

> Status: BLOCKED
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
- [x] `read` `source/player/renderer.h` and DLNA transport actions — map existing thread-safe playback/query APIs.
- [x] `rg` UxPlay current remote-video handlers and RPiPlay support gaps — define endpoint/body/status compatibility matrix.
- [x] `write` `source/protocol/airplay/media/remote_video.[ch]` — validate URL, parse plist/text parameters and map commands.
- [x] `edit` `source/protocol/airplay/protocol/handlers.c` — route remote-video endpoints and return playback state/time.
- [x] `edit` `scripts/smoke_airplay.py` — test play/rate/scrub/info/stop sequences with a local HTTP/HLS fixture.
- [ ] `bash` host smoke and Switch tests from Safari/video apps — host direct HLS is verified; real iPhone/Switch acceptance is unavailable.

## Quality Checklist
- [x] Evidence-before-edit: renderer API and AirPlay endpoint contracts documented.
- [x] Existing pattern / reuse checked: DLNA AVTransport and IPTV direct URL playback.
- [x] Contract understood: only http/https URLs accepted; player state owns authoritative position/duration.
- [x] Risk reviewed: SSRF-like local URL access, malformed position, stale callbacks, app-specific headers and HLS redirects.
- [x] Mitigation recorded: scheme/length validation, session generation, bounded metadata and local fixtures.

## Validation Checklist
- [x] `make test-airplay` and remote-video smoke exit 0.
- [ ] Strict Switch build plays a direct MP4 and HLS URL controlled from an iPhone.
- [ ] Existing CCTV/iQiyi/DLNA/IPTV playback regression tests remain successful.

## Test Checklist
- [x] Endpoint tests cover play, pause/rate, scrub, info, stop, invalid URL, redirects and relative HLS segments.

## Implementation Notes
- Added an isolated session-owned remote-video state machine for classic text parameters and modern binary plist `/play` bodies.
- `/rate`, `/scrub`, `/playback-info` and `/stop` use injected player operations; authoritative time/state comes from the player snapshot rather than protocol-local timers.
- Only bounded `http://` and `https://` URLs without credentials or control characters are accepted. URLs are passed through unchanged; no HLS gateway, playlist rewrite or Content-Type override was added.
- A local HTTP fixture proves FFmpeg follows a redirect and resolves relative HLS segments with H.264/AAC directly.
- Host tests, ASan/UBSan, Clang static analysis and the strict libmpv/deko3d Switch build pass. Real iPhone control remains blocked by unavailable hardware and the inherited Step 7 compatibility boundary.

## Files Changed
- `source/protocol/airplay/media/remote_video.[ch]`
- `source/protocol/airplay/protocol/handlers.[ch]`
- `source/protocol/airplay/protocol/rtsp.c`
- `source/protocol/airplay/receiver.[ch]`
- `scripts/test_airplay_remote_video.c`
- `scripts/test_airplay_handlers.c`
- `scripts/smoke_airplay.py`
- `makefile`
