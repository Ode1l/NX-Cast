# Step 12: Clock, Jitter, and A/V Synchronization

> Status: PENDING
> Created: 2026-07-19

## Goal
统一 AirPlay 时钟域并控制抖动和漂移，使镜像音画长期同步且断点可恢复。

## Prerequisites
- Step 11 completed — video/audio packets carry source timestamps and play together.
- Real-device traces include source timing, local monotonic time, queue depth and observed A/V offset.

## Deliverables
- `source/protocol/airplay/mirror/clock.[ch]` 维护 NTP/源时钟到本地媒体时钟映射和不连续检测。
- 音视频队列具有明确延迟目标、丢弃/静音/重同步策略和可脱敏观测指标。

## Plan
- [ ] `rg` UxPlay/RPiPlay timing, NTP and jitter logic — compare equations, thresholds and legacy differences.
- [ ] `write` `source/protocol/airplay/mirror/clock.[ch]` — implement wrap-safe conversion, offset filter and discontinuity events.
- [ ] `edit` audio/video modules and stream bridge — map timestamps once and enforce bounded skew/latency policy.
- [ ] `edit` `TRACE_AIRPLAY` logs — add periodic queue/skew/drift counters without packet payloads or secrets.
- [ ] `edit` `scripts/test_airplay.c` — simulate jitter, loss, clock wrap, jump and slow drift with deterministic timelines.
- [ ] `bash` host tests and 30-minute Switch soak — tune only from measured skew/underrun evidence.

## Quality Checklist
- [ ] Evidence-before-edit: reference timing equations and real trace units documented.
- [ ] Existing pattern / reuse checked: local monotonic clock and libmpv timestamp handling.
- [ ] Contract understood: one authoritative mapping per session; discontinuity starts a new generation.
- [ ] Risk reviewed: integer wrap, feedback oscillation, growing latency, audible correction and false drift.
- [ ] Mitigation recorded: deterministic simulation, bounded correction, hysteresis and soak acceptance limits.

## Validation Checklist
- [ ] `make test-airplay` timing simulations exit 0.
- [ ] Strict Switch build runs 30 minutes without sustained drift or unbounded queues.
- [ ] Disconnect/reconnect and source pause resume without old-clock contamination.

## Test Checklist
- [ ] Simulations cover jitter, burst loss, clock wrap/jump, slow drift, pause/resume and restart.

## Implementation Notes
Pending.

## Files Changed
Pending.
