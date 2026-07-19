# Step 11: Mirror AAC Audio Path

> Status: PENDING
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
- [ ] `rg` UxPlay/RPiPlay audio setup/RTP paths — inventory codec fields, packet sequence, encryption and resend behavior.
- [ ] `write` `source/protocol/airplay/mirror/audio.[ch]` — implement bounded AAC packet parsing, decryption and discontinuity reporting.
- [ ] `edit` `source/protocol/airplay/media/stream_bridge.c` — add AAC stream metadata and interleaved timestamped packets.
- [ ] `edit` `source/protocol/airplay/discovery/mdns.c` — gate audio-related service/TXT records on compiled and validated support.
- [ ] `write` audio packet fixtures and extend `scripts/test_airplay.c` for loss, reorder, config change and teardown.
- [ ] `bash` tests, `ffprobe` dual-stream TS and Switch playback — verify audible output before sync tuning.

## Quality Checklist
- [ ] Evidence-before-edit: audio setup and packet format matrix recorded from both references and trace.
- [ ] Existing pattern / reuse checked: stream bridge, HOS audio output through existing libmpv backend.
- [ ] Contract understood: audio exists only inside a video/mirror session and shares its generation/lifecycle.
- [ ] Risk reviewed: packet loss, sample-format mismatch, queue growth, clicks and stale audio after reconnect.
- [ ] Mitigation recorded: bounded jitter input, silence/drop policy, generation IDs and fixture tests.

## Validation Checklist
- [ ] `make test-airplay` exits 0.
- [ ] `ffprobe` reports one H.264 and one AAC stream in the generated TS.
- [ ] Strict Switch build plays mirror video with audible audio.

## Test Checklist
- [ ] AAC fixtures cover normal packets, loss/reorder, malformed size, config change, mute and reconnect.

## Implementation Notes
Pending.

## Files Changed
Pending.
