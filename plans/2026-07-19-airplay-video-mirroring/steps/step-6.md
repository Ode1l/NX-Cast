# Step 6: Native mDNS Discovery

> Status: PENDING
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
- [ ] `read` `source/protocol/dlna/discovery/ssdp.c` — reuse cancellation, interface binding and thread lifecycle conventions.
- [ ] `rg` UxPlay/RPiPlay for service names and TXT fields — derive a documented minimal record set and feature-bit rationale.
- [ ] `write` `source/protocol/airplay/discovery/mdns.h` and `.c` — implement bounded DNS packet encode/decode and responder thread.
- [ ] `edit` `source/protocol/airplay/airplay.c` — start mDNS only after the control listener has a valid port and identity.
- [ ] `edit` `scripts/smoke_airplay.py` — query multicast/unicast responses, TTL, PTR/SRV/TXT/A consistency and goodbye.
- [ ] `bash` host tests, smoke test and strict Switch build — then verify discovery from one iPhone.

## Quality Checklist
- [ ] Evidence-before-edit: SSDP lifecycle and reference TXT inventories recorded.
- [ ] Existing pattern / reuse checked: native sockets/threading from DLNA discovery.
- [ ] Contract understood: advertised port/identity match active server; stop sends goodbye before socket teardown.
- [ ] Risk reviewed: malformed DNS compression, packet amplification, duplicate names and false capability flags.
- [ ] Mitigation recorded: strict packet limits, local-link replies, conflict suffixing and stage-gated TXT fields.

## Validation Checklist
- [ ] `make test-airplay` exits 0.
- [ ] `python3 scripts/smoke_airplay.py --mdns` exits 0 on a LAN-capable host.
- [ ] Strict Switch build exits 0 and iPhone lists NX-Cast once.

## Test Checklist
- [ ] DNS unit fixtures, announce/query/goodbye smoke and duplicate-name behavior pass.

## Implementation Notes
Pending.

## Files Changed
Pending.
