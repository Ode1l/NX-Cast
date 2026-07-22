# Step 2: Make Closed Peer Test Portable

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Prove terminal log-mirror failure after peer closure using a bounded portable synchronization/retry instead of requiring the first send to fail.

## Prerequisites
- Step 1 completed with sanitizer/toolchain behavior documented.
- Files to modify: `scripts/test_log_mirror.c`; production `source/log/mirror.c` only if reproduction proves a real classification bug.
- Existing nonblocking and backpressure assertions remain authoritative.

## Deliverables
- A Cygwin-compatible closed-peer test that cannot wait indefinitely and cannot pass without observing `LOG_MIRROR_WRITE_FAILED`.
- After this step: `make test-log-mirror` and `make test-airplay` pass without changing production mirror behavior.

## Plan
- [x] `bash` focused reproduction — record the sequence of write results after peer close on Cygwin.
- [x] `rg` socket test helpers — reuse an existing bounded wait pattern if one exists.
- [x] `edit` `scripts/test_log_mirror.c` — add a small bounded eventual-failure loop with explicit allowed intermediate results.
- [x] `bash` focused and aggregate host tests — run `make test-log-mirror` and `make test-airplay`.
- [x] `bash` Profile 13 Switch build — confirm the host-only change does not disturb the target build when practical.

## Quality Checklist
- [x] Evidence-before-edit: target read `scripts/test_log_mirror.c`, impact search `rg LOG_MIRROR_WRITE_FAILED`, validation `make test-log-mirror`
- [x] Existing pattern / reuse checked: search scripts for bounded retry/deadline helpers
- [x] Contract understood: asynchronous stream close, nonblocking writes, terminal versus transient classification
- [x] Risk reviewed: flaky timing, infinite loops, and accidentally accepting perpetual success/drop
- [x] Mitigation recorded: fixed attempt/deadline bound plus mandatory terminal failure assertion

## Validation Checklist
- [x] `make test-log-mirror` exits 0 repeatedly on Cygwin
- [x] `make test-airplay` exits 0 or any unrelated dependency failure is explicitly separated
- [x] Relevant Profile 13 Switch build exits 0 when run

## Test Checklist
- [x] Closed-peer test observes `LOG_MIRROR_WRITE_FAILED` within the bound
- [x] Existing success and backpressure tests continue passing

## Implementation Notes
The original first-write assertion reproduced reliably on Cygwin. Reused the project's POSIX `nanosleep` test pattern and added a 10 ms retry step with a hard 1000 ms bound. Intermediate `OK` and `DROPPED` results are accepted only while close notification propagates; the test still fails unless it observes `LOG_MIRROR_WRITE_FAILED`. The focused binary passed once through Make and ten additional consecutive runs. Aggregate C dependencies passed; the remaining aggregate was blocked only by pre-existing missing host `python3` and host mbedTLS development files. Profile 13 rebuilt successfully. A protective commit was skipped because the dirty worktree contains user/prior changes.

## Files Changed
- `scripts/test_log_mirror.c`
