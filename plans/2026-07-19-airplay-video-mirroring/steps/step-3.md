# Step 3: Persistent AirPlay Control Server

> Status: PENDING
> Created: 2026-07-19

## Goal
实现独立于 DLNA 的持久 RTSP/HTTP 控制连接、请求解析和有序响应。

## Prerequisites
- Step 2 completed — bounded plist values can be attached to control requests.
- Files to inspect: `source/protocol/http/http_server.c`, UxPlay/RPiPlay RTSP methods and headers.

## Deliverables
- `source/protocol/airplay/protocol/rtsp.[ch]` 处理 persistent connections、Content-Length、CSeq、session state 和 method routing。
- `scripts/smoke_airplay.py` 可验证同一 TCP 连接上的多请求、分片接收、错误长度和关闭行为。

## Plan
- [ ] `read` `source/protocol/http/http_server.c` — document why its one-request lifecycle is not reused.
- [ ] `rg` reference handlers for required methods/headers — define a minimal endpoint and state table without copying implementation.
- [ ] `write` `source/protocol/airplay/protocol/rtsp.h` and `.c` — add bounded parser, response builder and session dispatcher.
- [ ] `write` `source/protocol/airplay/server.c` and `.h` — own listener/client threads, deadlines and cancellation.
- [ ] `write` `scripts/smoke_airplay.py` — exercise fragmented and pipelined local requests over one socket.
- [ ] `bash` `make test-airplay` and local smoke command — expect deterministic status/CSeq responses and clean stop.

## Quality Checklist
- [ ] Evidence-before-edit: target read `http_server.c`; method inventory and callers recorded.
- [ ] Existing pattern / reuse checked: reuse socket/log/thread helpers, not DLNA connection semantics.
- [ ] Contract understood: one owner per connection; bounded header/body; cancellation unblocks accept/read.
- [ ] Risk reviewed: slowloris, malformed lengths, thread leaks and response reordering.
- [ ] Mitigation recorded: timeouts, fixed limits, serialized connection state and stop tests.

## Validation Checklist
- [ ] `make test-airplay` exits 0.
- [ ] `python3 scripts/smoke_airplay.py --host 127.0.0.1 --port 7000` exits 0.
- [ ] Strict Switch build exits 0.

## Test Checklist
- [ ] Parser and socket smoke cover split headers/body, multiple requests, invalid CSeq/length and peer disconnect.

## Implementation Notes
Pending.

## Files Changed
Pending.
