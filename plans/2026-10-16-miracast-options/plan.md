# Plan: Offline WFD OPTIONS Handshake

> Status: COMPLETED
> Created: 2026-10-16
> Last Updated: 2026-10-16

## Goal
Implement and verify the first bidirectional WFD control exchange without changing production behavior.

## Assumptions
- Wireless discovery and group formation remain unverified; no device is available.
- A future transport adapter will supply parsed M2 responses and monotonic time.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] Valid M1 produces a matching response and independent M2 request.
- [x] Only a matching successful M2 with required methods completes the exchange.
- [x] Timeout, disconnect, invalid input and output capacity have deterministic behavior.
- [x] Existing app build, CI, packaging and production sources are unchanged.
### Non-goals
- Wireless IPC, live sockets, full M3-M7 negotiation, media and application integration.
### Edge Cases
- Fragmented input is owned by the common parser; stale CSeq, duplicate OPTIONS, unsupported Require, short output and deadline overflow are tested.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Isolation | Production integration or offline prototype | tools/miracast_protocol; no threads or global state | User authorized offline development |
| Scope | Whole receiver or bounded exchange | Complete M1/M2 core, explicit response-adapter boundary | Continuation of staged implementation |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Implement, document and test OPTIONS exchange |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Test | `make -C tools/miracast_protocol test` | New isolated host target | yes |
| Safety | `make -C tools/miracast_protocol sanitize` | New isolated ASan/UBSan target | yes |
| Hygiene | `git diff --check` | Git | yes |

## Context & Learnings
### Key Decisions
- Reuse common RTSP request parsing, not AirPlay routes or session lifetimes.
### Gotchas & Warnings
- OPTIONS success is not media readiness or proof of WFD platform support.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| source/protocol/rtsp/request.h | Existing request contract | Read |
| docs/miracast-platform-feasibility.md | Platform gates | Read |
| tools/miracast_protocol | Isolated new prototype | No existing equivalent found in tools or local references |
### Verified Facts
- codex/miracast was clean before this work.
- MiracleCast ctl-sink.c sends an OPTIONS reply then its own OPTIONS request with WFD Require (commit 0b7f1f1f6586dc65ff480f3cda5c2170a70aa020).

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-16 | 1 | Started isolated OPTIONS exchange; no live receiver integration. |
| 2026-10-16 | 1 | Host and ASan/UBSan tests pass; devkitA64 compiles options.c with strict warnings. Production diff empty. Remaining protocol and hardware gates documented. |
