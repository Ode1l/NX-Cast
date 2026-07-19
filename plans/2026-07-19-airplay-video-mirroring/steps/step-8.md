# Step 8: H.264 Mirror Transport

> Status: COMPLETED
> Created: 2026-07-19

## Goal
接收、解密并重组 AirPlay 镜像视频为带时间戳的完整 H.264 配置和访问单元。

## Prerequisites
- Step 7 completed — RECORD creates a validated mirror transport configuration and session keys.
- Sanitized encrypted packet/header fixtures or a user-provided trace are available.

Step 7 的真实 iPhone FairPlay 门仍阻塞；本步骤使用经过边界验证的合成 session key 和脱敏夹具独立实现、测试媒体层，不宣称真机密钥路径已打通。

## Deliverables
- `source/protocol/airplay/mirror/mirror_session.[ch]` 管理媒体套接字、序列、超时、停止和重连。
- `source/protocol/airplay/mirror/video.[ch]` 输出 SPS/PPS、IDR 和普通帧事件，可选将调试流写入 SD。

## Plan
- [x] `rg` UxPlay/RPiPlay mirror modules for framing states, header fields and version differences — record behavior, not source text.
- [x] `write` `source/protocol/airplay/mirror/mirror_session.[ch]` — implement bounded encrypted-frame receive loop and cancellation.
- [x] `write` `source/protocol/airplay/mirror/video.[ch]` — parse codec config, normalize Annex B and emit timestamped access units.
- [x] `write` `scripts/fixtures/airplay/mirror/` — add sanitized config/frame/truncated packet fixtures; reordering is synthesized by timestamp in the test.
- [x] `write` `scripts/test_airplay_mirror.c` — verify frame boundaries, SPS/PPS changes, keyframe recovery, disconnect and reconnect.
- [x] `bash` tests and fixture dump — validate output with host `ffprobe` without involving UI.

## Quality Checklist
- [x] Evidence-before-edit: framing/version matrix was recorded from both references; no real-device maximum was available, so the implementation records an explicit 8 MiB hard cap rather than claiming an observed maximum.
- [x] Existing pattern / reuse checked: Step 3 socket lifecycle and Step 4 session crypto wrappers.
- [x] Contract understood: callback receives complete immutable access units with monotonic media timestamps.
- [x] Risk reviewed: oversized frames, partial reads, nonce reuse, lost keyframes, queue growth and stop races.
- [x] Mitigation recorded: hard limits, direct bounded callback, resync-on-IDR policy and cancellation/reconnect tests.

## Validation Checklist
- [x] `make test-airplay` exits 0.
- [x] Extracted fixture H.264 is accepted by `ffprobe` as 64x64 Constrained Baseline H.264.
- [x] Strict Switch build exits 0.

## Test Checklist
- [x] Fixtures/tests cover codec config, IDR/P frame, SPS/PPS replacement, malformed length, truncation, reordering and reconnect.

## Implementation Notes
- The media listener accepts one TCP sender at a time, reads exact 128-byte headers and bounded payloads, and returns to accept after a disconnect or malformed sender.
- AES key and IV are SHA-512 derivations over `AirPlayStreamKey/IV<streamConnectionID>` plus the 16-byte session key. AES-CTR state advances only for encrypted type-0 video packets and is reset on reconnect.
- AVCDecoderConfigurationRecord SPS/PPS values are normalized to Annex B. Non-IDR frames are dropped until configuration plus an IDR is available; configuration changes increment a generation and re-enter that recovery state.
- The callback buffer is immutable but valid only for the callback duration. Step 9 will copy it into a bounded stream bridge.
- The build system uses basename-only objects, so `mirror_session.c` is intentionally named to avoid collision with `player/core/session.c`.
- Real iPhone encrypted packets remain an external validation item because Step 7 cannot yet produce a clean-room FairPlay session key.

## Files Changed
- `makefile`
- `source/protocol/airplay/mirror/mirror_session.[ch]`
- `source/protocol/airplay/mirror/video.[ch]`
- `scripts/test_airplay_mirror.c`
- `scripts/fixtures/airplay/mirror/*.hex`
