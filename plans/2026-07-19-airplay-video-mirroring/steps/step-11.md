# Step 11: Mirror AAC Audio Path

> Status: BLOCKED
> Created: 2026-07-19

## Goal
在不提供独立音乐接收模式的前提下，接收 iPhone 镜像会话的 AAC 音频并加入实时媒体流。

## Prerequisites
- Step 10 completed — H.264 mirror is stable and owns a restartable stream bridge.
- Sanitized audio SETUP and encrypted packet fixtures are available from the same mirror session.

## Deliverables
- `source/protocol/airplay/mirror/audio.[ch]` 解析音频配置、解密/重组 AAC 帧并输出时间戳。
- MPEG-TS bridge 包含 AAC 音轨；仅在音频路径通过验收后增加镜像所需的 `_raop._tcp` 能力记录。

## Plan
- [x] `rg` UxPlay/RPiPlay audio setup/RTP paths — inventoried codec fields, RTP sequence/timestamp, per-packet CBC reset and clear tail behavior.
- [x] `write` `source/protocol/airplay/mirror/audio.[ch]` — implemented bounded AAC packet parsing, decryption and discontinuity reporting.
- [x] `edit` `source/protocol/airplay/media/stream_bridge.c` — added AAC stream metadata and interleaved timestamped packets.
- [x] `edit` `source/protocol/airplay/discovery/mdns.c` — inspected capability boundary and intentionally retained `_raop._tcp` as disabled until real audible validation.
- [x] `write` audio packet fixtures and extend host coverage for loss, reorder, unsupported config, mute and reconnect.
- [ ] `bash` tests, `ffprobe` dual-stream TS and Switch playback — host tests/probe and strict build pass; real audible Switch playback is blocked by Step 7 FairPlay compatibility.

## Quality Checklist
- [x] Evidence-before-edit: audio setup and packet format matrix recorded from both references; a real trace remains unavailable behind Step 7.
- [x] Existing pattern / reuse checked: stream bridge, HOS audio output through existing libmpv backend.
- [x] Contract understood: audio exists only inside a video/mirror session and shares its generation/lifecycle.
- [x] Risk reviewed: packet loss, sample-format mismatch, queue growth, clicks and stale audio after reconnect.
- [x] Mitigation recorded: bounded jitter input, drop/discontinuity policy, generation IDs and fixture tests.

## Validation Checklist
- [x] `make test-airplay` exits 0, including ASan/UBSan.
- [x] `ffprobe` reports one H.264 and one AAC stream in the generated TS.
- [ ] Strict Switch build links the audio path, but audible real-device playback cannot be accepted until Step 7 is resolved.

## Test Checklist
- [x] AAC fixtures cover normal packets, loss/reorder, malformed size, unsupported config, mute and reconnect.

## Implementation Notes
- Added AES-CBC packet decryption with a fresh IV per RTP packet and preserved the observed unaligned clear tail.
- Added AAC-LC/AAC-ELD format validation, a fixed 64-packet reorder window, duplicate suppression and discontinuity reporting.
- Added audio SETUP handling and returned UDP data/control ports independent of stream-array order.
- Added lazy dual-stream MPEG-TS header creation so AAC metadata is present before libmpv consumes the stream.
- `_raop._tcp` remains intentionally unadvertised because standalone audio is a non-goal and real mirror audio has not passed hardware acceptance.

## Files Changed
- `makefile`
- `scripts/fixtures/airplay/mirror/aac-lc-frame.hex`
- `scripts/test_airplay_audio.c`
- `scripts/test_airplay_crypto.c`
- `scripts/test_airplay_handlers.c`
- `source/protocol/airplay/media/mirror_runtime.[ch]`
- `source/protocol/airplay/media/stream_bridge.[ch]`
- `source/protocol/airplay/mirror/audio.[ch]`
- `source/protocol/airplay/protocol/handlers.[ch]`
- `source/protocol/airplay/receiver.[ch]`
- `source/protocol/airplay/security/crypto.[ch]`
