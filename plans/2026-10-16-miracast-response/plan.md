# Plan: WFD Response Wire Decoding

> Status: COMPLETED
> Created: 2026-10-16
> Last Updated: 2026-10-16

## Goal
Complete the offline OPTIONS exchange using bounded RTSP response bytes.
## Assumptions
- No hardware testing is available; native discovery remains unverified.
## Open Questions
None.
## Spec-Lite
### Acceptance Criteria
- [x] Parse one response, preserve trailing messages and handle partial input.
- [x] Reject ambiguous framing without advancing OPTIONS state.
- [x] Test a byte-level M1/M2 exchange and a following M3 request.
### Non-goals
- Sockets, wireless operations, M3 capability negotiation and playback integration.
### Edge Cases
- Duplicate critical fields, binary body, overflow, incomplete headers/body, stale CSeq.
## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Placement | Shared parser refactor or isolated decoder | Isolated bounded response view; existing request parser unchanged | User approved offline continuation |
## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | steps/step-1.md | COMPLETED | Response decoder, handshake adapter and tests |
## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Tests | make -C tools/miracast_protocol test sanitize | Existing local Makefile | yes |
| Build | aarch64-none-elf-gcc -std=c11 -Wall -Wextra -Werror -Isource -c | Existing SDK | yes |
| Hygiene | git diff --check | Git | yes |
## Context & Learnings
### Key Decisions
- Parse exactly one message; no heap ownership for response bodies.
### Gotchas & Warnings
- Borrowed body spans cannot outlive the input buffer. No network readiness claims.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| source/protocol/rtsp/rtsp_request.c | Framing conventions and limits | Read |
| tools/miracast_protocol/options.h | Normalized response boundary | Read |
### Verified Facts
- No response parser was found in source/protocol by rg.
- RFC 2326 sections 4, 7 and 12 define status line, length framing and CSeq.
## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-16 | 1 | Start byte-level response slice on clean codex/miracast. |
| 2026-10-16 | 1 | Decoder and wire adapter pass host/ASan/UBSan tests and devkitA64 object compilation. Public list fields merge with a total length bound. Production diff is empty. |
