# Step 8: H.264 Mirror Transport

> Status: PENDING
> Created: 2026-07-19

## Goal
接收、解密并重组 AirPlay 镜像视频为带时间戳的完整 H.264 配置和访问单元。

## Prerequisites
- Step 7 completed — RECORD creates a validated mirror transport configuration and session keys.
- Sanitized encrypted packet/header fixtures or a user-provided trace are available.

## Deliverables
- `source/protocol/airplay/mirror/session.[ch]` 管理媒体套接字、序列、超时、停止和重连。
- `source/protocol/airplay/mirror/video.[ch]` 输出 SPS/PPS、IDR 和普通帧事件，可选将调试流写入 SD。

## Plan
- [ ] `rg` UxPlay/RPiPlay mirror modules for framing states, header fields and version differences — record behavior, not source text.
- [ ] `write` `source/protocol/airplay/mirror/session.[ch]` — implement bounded encrypted-frame receive loop and cancellation.
- [ ] `write` `source/protocol/airplay/mirror/video.[ch]` — parse codec config, normalize Annex B and emit timestamped access units.
- [ ] `write` `scripts/fixtures/airplay/mirror/` — add sanitized config/frame/truncated/reordered packet fixtures.
- [ ] `edit` `scripts/test_airplay.c` — verify frame boundaries, SPS/PPS changes, keyframe recovery and disconnect.
- [ ] `bash` tests and optional fixture dump — validate output with host `ffprobe`/decoder without involving UI.

## Quality Checklist
- [ ] Evidence-before-edit: framing/version matrix and max observed packet sizes recorded.
- [ ] Existing pattern / reuse checked: Step 3 socket lifecycle and Step 4 session crypto wrappers.
- [ ] Contract understood: callback receives complete immutable access units with monotonic media timestamps.
- [ ] Risk reviewed: oversized frames, partial reads, nonce reuse, lost keyframes, queue growth and stop races.
- [ ] Mitigation recorded: hard limits, bounded queue, resync-on-IDR policy and cancellation tests.

## Validation Checklist
- [ ] `make test-airplay` exits 0.
- [ ] Extracted fixture H.264 is accepted by `ffprobe` or the available host decoder.
- [ ] Strict Switch build exits 0.

## Test Checklist
- [ ] Fixtures cover codec config, IDR/P frame, SPS/PPS replacement, malformed length, truncation and reconnect.

## Implementation Notes
Pending.

## Files Changed
Pending.
