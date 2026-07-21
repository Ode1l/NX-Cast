# Step 4: Network And Collection Boundary Hardening

> Status: COMPLETED
> Created: 2026-07-21

## Goal
Apply checked arithmetic and invariant validation to remaining high-risk packet, download, and collection boundaries found by the audit.

## Prerequisites
- Step 3 completed and checked-size helper established.
- Existing AirPlay DNS/mDNS, IPTV fetch, and DLNA state tests/callers inspected.

## Deliverables
- Capacity subtraction cannot underflow in DNS writers.
- Download and collection size checks cannot wrap.
- After this step: existing parser/protocol tests pass under sanitizers.

## Plan
- [x] `edit` `source/protocol/airplay/discovery/dns.c` — validate writer invariants before capacity subtraction/patch arithmetic.
- [x] `edit` `source/protocol/airplay/discovery/mdns.c` — check TXT/hex size arithmetic before writes.
- [x] `edit` `source/iptv/fetch.c` — make download-limit and temporary-path checks overflow/truncation safe.
- [x] `edit` `source/protocol/dlna/control/protocol_state.c` — guard collection growth and allocation multiplication.
- [x] `edit` existing focused tests and `bash` sanitizer/aggregate suite — cover rejected boundary states.

## Quality Checklist
- [x] Evidence-before-edit: each target and caller/test read.
- [x] Existing pattern / reuse checked: shared checked-size helper used instead of duplicate arithmetic.
- [x] Contract understood: malformed/oversized external data is rejected without partial state publication.
- [x] Risk reviewed: parser compatibility, integer wrap, OOM, and partial files.
- [x] Mitigation recorded: existing wire fixtures plus new boundary assertions.

## Validation Checklist
- [x] `make test-c-safety test-airplay` exits 0.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] ASan/UBSan aggregate host tests pass with leak detection disabled.

## Implementation Notes
- DNS writers now reject corrupt internal offsets before subtraction and clear output metadata on failure.
- mDNS TXT and hexadecimal formatting use checked add/multiply operations, including the zero-length separator edge case.
- IPTV downloads reject truncated temporary paths and use subtraction-based size-limit checks.
- DLNA state collection growth is overflow checked, and a partially constructed state variable is freed before failure returns.

## Files Changed
- `source/protocol/airplay/discovery/dns.c`
- `source/protocol/airplay/discovery/mdns.c`
- `source/iptv/fetch.c`
- `source/protocol/dlna/control/protocol_state.c`
- `scripts/test_airplay_dns.c`
