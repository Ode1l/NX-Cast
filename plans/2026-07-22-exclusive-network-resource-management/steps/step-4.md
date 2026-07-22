# Step 4: Bounded network teardown and cancellation

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Make every instrumented owned socket stop through a bounded signal, shutdown, join, close, and reset sequence that cannot strand a worker or BSD session.

## Prerequisites
- Step 3 completed with operation/socket diagnostics.
- Files to modify are limited to instrumented mDNS, SSDP, DLNA HTTP/event, and AirPlay control paths where the audit finds an unbounded or incorrectly ordered operation.
- Existing shutdown-order test convention is available in `scripts/test_shutdown_order.py`.

## Deliverables
- Each receiver publishes a stop request before socket shutdown and never holds its state mutex while joining.
- Blocking socket calls have a timeout or are interrupted by `shutdown(SHUT_RDWR)` before join.
- Start failure and repeated stop paths balance sockets, threads, diagnostics, and state.
- After this step: focused lifecycle tests and `make test-shutdown-order` pass with no unbounded call found in the owned receiver paths.

## Plan
- [x] `rg source/protocol` — audited each instrumented `select|accept|recv|recvfrom|send|sendto|connect` call against its stop path and diagnostic duration.
- [x] `edit source/protocol/airplay/discovery/mdns.c source/protocol/dlna/discovery/ssdp.c` — made running/socket state atomic, rejected post-stop readiness, and delayed close until after join.
- [x] `edit source/protocol/http/http_server.c source/protocol/dlna/control/event_server.c` — added active-client/outbound-fd ownership, bounded connect/send/recv, and stop-time interruption.
- [x] `edit source/protocol/airplay/server.c` — transferred listener/client fd ownership at stop, shutdown before joining, and close only after every worker exits.
- [x] `edit source/iptv/fetch.[ch] source/iptv/iptv.c` — made background FFmpeg fetches cancellable and required quiescence before exclusive playback can open.
- [x] `edit scripts/test_shutdown_order.py` and focused host tests — assert signal/shutdown/join/close/reset ordering and blocked AirPlay-client teardown.
- [x] `bash make test-network-diagnostics test-airplay test-shutdown-order` — focused diagnostics, mDNS, AirPlay server, coordinator, and shutdown tests pass; aggregate `test-airplay` retains the unrelated Cygwin log-mirror blocker recorded in Step 2.

## Quality Checklist
- [x] Evidence-before-edit: each change follows a concrete active-client, outbound-connect, data-race, early-close, or in-flight IPTV-fetch gap.
- [x] Existing pattern / reuse checked: reused `shutdown(SHUT_RDWR)`, atomic running flags, bounded select/timeouts, FFmpeg interrupt callbacks, and existing join helpers.
- [x] Contract understood: close alone is not relied upon to wake a blocking call; no join occurs under a protocol mutex.
- [x] Risk reviewed: use-after-close, fd reuse, double close, self-join, lost final byebye/announcement, and fetch cancellation during cache writes.
- [x] Mitigation recorded: atomic fd exchange, stop flag first, delayed close, temporary-file cleanup, retryable quiesce failure, and lifecycle tests.

## Validation Checklist
- [x] `make test-network-diagnostics` exits 0.
- [ ] `make test-airplay` exits 0 — unrelated Cygwin `test_log_mirror.c` closed-peer assertion remains.
- [x] `make test-airplay-mdns-suspend test-airplay-server-lifecycle test-protocol-coordinator` exits 0.
- [x] `make test-shutdown-order` equivalent bundled-Python invocation exits 0.
- [x] Profile 13 Switch build links after all teardown/cancellation changes.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] Stop during AirPlay accept/recv returns and joins within the one-second host test timeout; select/recv workers are shutdown before join by static order checks.
- [x] Stop and restart leave zero AirPlay diagnostic sockets/operations; mDNS start/stop also balances exactly.
- [x] Repeated stop is idempotent and fd ownership is exchanged exactly once, with close deferred until joined workers cannot reuse it.
- [x] A failed IPTV background quiesce keeps the resource transition unapplied and is retried rather than opening media concurrently.

## Implementation Notes
- DLNA HTTP now tracks the accepted client separately from the listener. Stop atomically takes both descriptors, shuts them down, joins the worker, then closes them.
- DLNA event notifications expose their active outbound descriptor; connect is nonblocking with a one-second select timeout, send/recv use one-second socket timeouts, and stop can interrupt either phase.
- SSDP no longer `memset`s shared running/socket state; both fields are atomic and readiness is rechecked after select before using the descriptor.
- AirPlay control stop takes all client/listener descriptors before shutdown and defers close until the listener has joined every client worker.
- IPTV uses `AVIOInterruptCB`, a three-second FFmpeg I/O fallback, temporary-file cleanup, and a four-second coordinator quiesce bound. A suspended in-progress job is requeued for Home rather than lost.
- Resource command waits were raised to six seconds so receiver teardown plus IPTV quiescence can finish without reproducing the prior “first play fails, second works” behavior.

## Files Changed
- `source/protocol/airplay/discovery/mdns.c`
- `source/protocol/dlna/discovery/ssdp.c`
- `source/protocol/http/http_server.c`
- `source/protocol/dlna/control/event_server.c`
- `source/protocol/airplay/server.c`
- `source/iptv/fetch.[ch]`
- `source/iptv/iptv.[ch]`
- `source/app/protocol_coordinator.[ch]`
- `source/main.c`
- `scripts/test_airplay_server_lifecycle.c`
- `scripts/test_protocol_coordinator.c`
- `scripts/test_shutdown_order.py`
- `makefile`
