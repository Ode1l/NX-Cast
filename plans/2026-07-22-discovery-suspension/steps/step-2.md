# Step 2: Atomic Discovery Worker Suspension

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Add nonblocking suspend/resume controls to mDNS and SSDP workers while preserving their sockets, threads, and control-plane services.

## Prerequisites
- Step 1 completed with the coordinator callback contract tested.
- Files to modify: `source/protocol/airplay/discovery/mdns.h`, `source/protocol/airplay/discovery/mdns.c`, `source/protocol/dlna/discovery/ssdp.h`, `source/protocol/dlna/discovery/ssdp.c`, `scripts/test_airplay_mdns_suspend.c`, and the focused Makefile target.

## Deliverables
- Atomic suspension APIs for both discovery workers.
- Worker-side sleeping while suspended and worker-side re-announcement after resume.
- After this step: available host discovery tests pass and sources compile in the final Switch build.

## Plan
- [x] `read`/`rg` discovery state, start/stop, worker loops, diagnostics, and existing tests — confirm lifecycle and timing conventions.
- [x] `edit` mDNS header/source — add atomic suspend state, bounded sleep loop, diagnostics, and resume announcement.
- [x] `edit` SSDP header/source — add atomic suspend state, bounded sleep loop, and resume alive announcement.
- [x] `bash` focused mDNS host regression plus Switch SSDP syntax check — expect zero failures; record unavailable host SSDP coverage explicitly.

## Quality Checklist
- [x] Evidence-before-edit: target loops and start/stop paths read; impact callers searched; validation targets discovered.
- [x] Existing pattern / reuse checked: use existing atomics, sleep helpers, announcement routines, and worker threads.
- [x] Contract understood: setter performs atomic state change only; worker owns networking and announcements.
- [x] Risk reviewed: select latency, stop while suspended, data races, duplicate announcements.
- [x] Mitigation recorded: bounded sleep, running checks, atomic stores, local resume edge tracking.

## Validation Checklist
- [x] Relevant host discovery test target exits 0; SSDP has no host implementation, so its Switch syntax check exits 0.
- [x] Headers and implementations expose matching signatures and no new direct UI-thread network operations.

## Test Checklist
- [x] Suspended mDNS skips select/receive/periodic announcements.
- [x] Suspended SSDP skips select/receive/alive announcements by inspection and Switch compiler validation.
- [x] Resume performs one worker-owned re-announcement and returns to normal polling.
- [x] Stop remains bounded while suspended.

## Implementation Notes
- Both public setters perform only an atomic store. The existing workers observe the request after at most their bounded 200 ms/100 ms wait and retain their sockets and threads.
- mDNS exposes `suspended` and a `suspended` phase in runtime diagnostics; the host regression verifies its select and sent-packet counters stay fixed while suspended, then verifies the worker sends the two AirPlay/RAOP records after resume.
- SSDP emits its alive set after the worker observes the resume edge and resets its 30-second notify deadline.
- Validation: `make ... test-airplay-mdns-suspend` passed with strict host warnings; `aarch64-none-elf-gcc ... -fsyntax-only ... ssdp.c` passed. Existing Python smoke could not run because this Windows/MSYS environment has no Python executable, so the focused C regression removes that dependency for this behavior.

## Files Changed
- `source/protocol/airplay/discovery/mdns.h`
- `source/protocol/airplay/discovery/mdns.c`
- `source/protocol/dlna/discovery/ssdp.h`
- `source/protocol/dlna/discovery/ssdp.c`
- `scripts/test_airplay_mdns_suspend.c`
- `makefile`
