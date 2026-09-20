# Step 2: Isolate the Local Media Data Plane

> Status: COMPLETED
> Created: 2026-08-22

## Goal
Serve loopback HLS through dedicated bounded capacity so protocol connections cannot starve media reads.

## Prerequisites
- Step 1 completed with the playable baseline restored.
- Files: AirPlay listener/integration lifecycle, local HLS routing, and server tests.
- Design: one parser/listener with disjoint loopback-media and LAN-control worker pools.

## Deliverables
- Local media requests never consume the four control slots.
- Data-plane pressure has explicit bounded metrics.
- After this step: held control/reverse sessions plus parallel playlist requests complete without 503.

## Plan
- [x] `read` `source/protocol/airplay/server.c`, integration lifecycle, and HLS routes — mapped ownership and shutdown.
- [x] `rg` project HTTP listeners — retained one parser/listener rather than duplicate the RTSP/HTTP implementation.
- [x] `edit` AirPlay server files — partitioned fixed worker slots by loopback versus LAN peer address.
- [x] `read` `remote_hls.c` — confirmed generated media URLs are generation-bound and use `127.0.0.1`.
- [x] `bash` `make test-airplay && git diff --check` — all checks passed.

## Quality Checklist
- [x] Evidence-before-edit: read lifecycle and correlated six local 503 responses with the four-slot pool
- [x] Existing pattern / reuse checked: reused the current parser/listener instead of creating another implementation
- [x] Contract understood: control carries state; data serves bounded generation-owned bytes
- [x] Risk reviewed: socket/thread exhaustion and shutdown races
- [x] Mitigation recorded: disjoint fixed bounds, common shutdown, lifecycle tests

## Validation Checklist
- [x] `make test-airplay` exits 0
- [x] `git diff --check` exits 0

## Test Checklist
- [x] Capacity invariant — four LAN control slots cannot consume four loopback media slots

## Implementation Notes
The server still has one listening socket and one request parser. Accepted LAN peers use slots 0-3; `127.0.0.0/8` peers use slots 4-7. Neither pool borrows from the other, and capacity rejection traces identify the exhausted pool. This removes the verified failure where four AirPlay control/reverse connections caused FFmpeg's first local HLS reads to receive 503.

## Files Changed
- `source/protocol/airplay/server.h`
- `source/protocol/airplay/server.c`
