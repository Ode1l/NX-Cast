# Step 6: Native mDNS Discovery

> Status: COMPLETED
> Created: 2026-07-19

## Goal
用 NX-Cast C 网络代码广播当前已实现的 AirPlay 能力，使同一 Wi-Fi 的 iPhone 可发现设备。

## Prerequisites
- Step 5 completed — pairing endpoints and device identity are stable.
- Files to inspect: `source/protocol/dlna/discovery/ssdp.c`, Switch socket APIs, reference TXT-record inventories.

## Deliverables
- `source/protocol/airplay/discovery/mdns.[ch]` 实现 DNS name compression、query response、announcement、goodbye 和名称冲突处理。
- `_airplay._tcp.local` TXT 记录只声明可用的控制/镜像阶段能力，不伪装完整 AirPlay 2。

## Plan
- [x] `read` `source/protocol/dlna/discovery/ssdp.c` — reuse cancellation, interface binding and thread lifecycle conventions.
- [x] `rg` UxPlay/RPiPlay for service names and TXT fields — derive a documented minimal record set and feature-bit rationale.
- [x] `write` `source/protocol/airplay/discovery/mdns.h` and `.c` — implement bounded DNS packet encode/decode and responder thread.
- [x] `edit` `source/protocol/airplay/airplay.c` — defer start until the Step 7 runtime owns a bound listener and loaded identity.
- [x] `edit` `scripts/smoke_airplay.py` — query multicast/unicast responses, TTL, PTR/SRV/TXT/A consistency and goodbye.
- [x] `bash` host tests, smoke test and strict Switch build — real iPhone discovery remains a Step 7 device gate.

## Quality Checklist
- [x] Evidence-before-edit: SSDP lifecycle and reference TXT inventories recorded.
- [x] Existing pattern / reuse checked: native sockets/threading from DLNA discovery.
- [x] Contract understood: advertised port/identity match active server; stop sends goodbye before socket teardown.
- [x] Risk reviewed: malformed DNS compression, packet amplification, duplicate names and false capability flags.
- [x] Mitigation recorded: strict packet limits, local-link replies, conflict suffixing and stage-gated TXT fields.

## Validation Checklist
- [x] `make test-airplay` exits 0.
- [x] `python3 scripts/smoke_airplay.py --mdns` exits 0 on a LAN-capable host.
- [ ] Strict Switch build exits 0 and iPhone lists NX-Cast once. Strict build passed; listing is deferred until Step 7 enables the runtime.

## Test Checklist
- [x] DNS unit fixtures, announce/query/goodbye smoke and duplicate-name behavior pass.

## Implementation Notes
- Added a bounded pure DNS codec for `_airplay._tcp.local` PTR, SRV, TXT and IPv4 A records. Responses use compression pointers and cache-flush classes for unique records; all packet/name/question/record counts are capped.
- The responder binds UDP 5353, joins 224.0.0.251, sends multicast with TTL 255, answers only relevant local-network questions, honors QU requests, periodically announces, and sends TTL=0 records before closing.
- Conflicting SRV/TXT/A ownership changes the service instance to `NX-Cast (2)` through `(99)` and immediately announces the new name.
- The minimal TXT inventory is `deviceid`, `features`, `flags`, `model`, `pk`, `pw`, `srcvers`, and `vv`. Step 6 tests advertise only legacy pairing bit 27; screen/video/HLS bits remain stage gated until those paths are implemented and accepted.
- A loopback UDP test transport is compiled only under `AIRPLAY_TESTING`. It observes announcement, QU query response, conflict rename and goodbye without depending on the host Bonjour daemon.
- Runtime integration is intentionally not enabled from the old lifecycle-only `airplay_start()`: Step 7 must first create identity/pairing, bind the RTSP listener, copy its actual port, and only then call `airplay_mdns_start()`.
- Normal tests, ASan/UBSan, Clang static analysis, UDP smoke and the strict Switch NRO build pass. iPhone discovery remains an explicit real-device gate.

## Files Changed
- `source/protocol/airplay/discovery/dns.[ch]`
- `source/protocol/airplay/discovery/mdns.[ch]`
- `scripts/test_airplay_dns.c`
- `scripts/airplay_mdns_smoke_server.c`
- `scripts/smoke_airplay_mdns.py`
- `scripts/smoke_airplay.py`
- `makefile`
