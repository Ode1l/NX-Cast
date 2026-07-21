# Step 2: Independent Runtime Liveness Markers

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Expose enough low-overhead runtime evidence to distinguish main-loop progress, logger progress, and mDNS worker progress in each diagnostic profile.

## Prerequisites
- Step 1 completed with a stable `NXCAST_DIAG_PROFILE` contract.
- Files to modify: existing runtime heartbeat/log stats and mDNS diagnostic counters only where the current evidence is insufficient.

## Deliverables
- Diagnostic startup and heartbeat records identify profile, main frame progress, logger health, protocol worker state, and mDNS phase/counter state.
- No independent periodic SD writer or additional network logger changes test timing.
- After this step: a stopped log can be interpreted against the last published subsystem state.

## Plan
- [x] `read` existing main/log/mDNS health state - identified the existing two-second snapshot and avoided duplicate mechanisms.
- [x] `edit` existing diagnostic snapshot structures and trace records - exposed mDNS lifecycle counters without adding blocking I/O.
- [x] `edit` focused host tests - validated running/socket state and complete stop publication.
- [x] `bash` focused host tests and isolated mode smoke runs - zero failures.

## Quality Checklist
- [x] Evidence-before-edit: main heartbeat, logger stats, and mDNS lifecycle state read; validation commands identified.
- [x] Existing pattern / reuse checked: extended existing snapshots/atomics rather than introducing a second logger.
- [x] Contract understood: diagnostics publish counters only and never wait on a worker.
- [x] Risk reviewed: data races, log volume, timing perturbation, and false confidence from async delivery.
- [x] Mitigation recorded: atomics, two-second cadence, compile-time diagnostic gating, and explicit interpretation limits.

## Validation Checklist
- [x] `make test-airplay` exits 0.
- [x] `make test-protocol-coordinator` exits 0 through the AirPlay suite dependency.
- [x] Socket-only, idle-thread, and receive-only smoke binaries start, publish `READY`, stop, and exit 0.
- [x] `git diff --check` reports no errors.

## Test Checklist
- [x] Diagnostic snapshots remain lock-free/nonblocking on protocol worker paths.
- [x] Existing normal-build log policy remains unchanged.

## Implementation Notes
Scope updated after the user took ownership of Windows/devkitPro tooling. The
existing main heartbeat now reads an atomic `AirPlayMdnsDiagnostics` snapshot.
The mDNS worker publishes lifecycle phase, heartbeat time, completed select
iterations, datagrams received, and datagrams sent. It performs no diagnostic
I/O of its own, so the observation mechanism does not add another logger or SD
writer.

## Files Changed
- `source/main.c`
- `source/protocol/airplay/discovery/mdns.h`
- `source/protocol/airplay/discovery/mdns.c`
- `scripts/airplay_mdns_smoke_server.c`
