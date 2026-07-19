# Step 3: Persistent AirPlay Control Server

> Status: COMPLETED
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
- [x] `read` `source/protocol/http/http_server.c` — its listener handles one request synchronously and closes every accepted socket, so it cannot preserve AirPlay CSeq/session/security state.
- [x] `rg` reference handlers for required methods/headers — initial inventory is OPTIONS, GET/POST, SETUP, RECORD, GET_PARAMETER, SET_PARAMETER and TEARDOWN with CSeq and Content-Length.
- [x] `write` `source/protocol/airplay/protocol/rtsp.h` and `.c` — added bounded parser, response builder and session dispatcher.
- [x] `write` `source/protocol/airplay/server.c` and `.h` — owns listener/client threads, deadlines, session-close cleanup and cancellation.
- [x] `write` `scripts/smoke_airplay.py` — exercises fragmented and pipelined local requests over one socket.
- [x] `bash` `make test-airplay` and local smoke command — deterministic status/CSeq responses and clean stop pass.

## Quality Checklist
- [x] Evidence-before-edit: target `http_server.c` read; reference method/header inventory and existing socket/thread usages recorded.
- [x] Existing pattern / reuse checked: reuse POSIX sockets and Switch `Thread` lifecycle conventions, not DLNA connection semantics.
- [x] Contract understood: one worker owns each connection/session; headers/bodies are bounded; stop shuts sockets to unblock accept/read.
- [x] Risk reviewed: correctness/API/security/performance/project-fit, including slowloris, malformed lengths, thread leaks and response reordering.
- [x] Mitigation recorded: 32 KiB headers, 1 MiB bodies, fixed client slots, I/O timeouts, serialized session state and stop tests.

## Validation Checklist
- [x] `make test-airplay` exits 0 under normal and ASan/UBSan flags.
- [x] `python3 scripts/smoke_airplay.py --host 127.0.0.1 --port 7000` exits 0, falling back to an ephemeral port when macOS Control Center owns 7000.
- [x] Clang static analysis and Python bytecode compilation exit 0.
- [x] Strict Switch build exits 0.

## Test Checklist
- [x] Parser and socket smoke cover every valid-message truncation, split headers/body, pipelined requests, invalid/duplicate lengths, invalid CSeq, slow request timeout, slot exhaustion, peer disconnect and stop cancellation.

## Implementation Notes
- Added a clean-room RTSP/HTTP parser with strict CRLF framing, case-insensitive header lookup, duplicate `Content-Length`/`CSeq` rejection, no transfer encoding, exact body accounting and explicit parse errors.
- Added response encoding with computed `Content-Length`, CSeq echoing and a minimal connection state machine for OPTIONS, SETUP, RECORD, parameter methods and TEARDOWN; later steps can replace routing through a callback.
- Added a separate portable TCP server with four fixed client slots, per-request deadlines, send timeouts, serialized per-connection sessions and a session-close callback for later secure cleanup.
- Request/response objects are heap allocated per client because their bounded header tables exceed the Switch worker stack. Socket closure uses atomic ownership transfer so stop and worker cleanup cannot close the same or a reused descriptor.
- The smoke harness validates real persistent TCP behavior and falls back to an ephemeral host port only when the requested local port is already occupied; production still uses the configured port.
- AirPlay server startup remains disconnected from `main.c`, so this step introduces no runtime listener or discovery behavior in NX-Cast.

## Files Changed
- `makefile`
- `source/protocol/airplay/protocol/rtsp.h`
- `source/protocol/airplay/protocol/rtsp.c`
- `source/protocol/airplay/server.h`
- `source/protocol/airplay/server.c`
- `scripts/test_airplay_rtsp.c`
- `scripts/airplay_smoke_server.c`
- `scripts/smoke_airplay.py`
- `plans/2026-07-19-airplay-video-mirroring/plan.md`
- `plans/2026-07-19-airplay-video-mirroring/steps/step-3.md`
- `plans/context.md`
