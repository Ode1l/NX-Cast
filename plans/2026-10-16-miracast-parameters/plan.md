# Plan: Offline WFD Parameter Negotiation

> Status: COMPLETED
> Created: 2026-10-16
> Last Updated: 2026-10-16

## Goal
Implement bounded M3 capability replies and atomic M4 selection for an explicit laboratory profile.
## Assumptions
- Hardware and media support are unverified; the configuration is a test fixture, not production advertising.
## Open Questions
None.
## Spec-Lite
### Acceptance Criteria
- [x] Query requested capabilities only; reject unsupported fields explicitly.
- [x] Validate video/audio/URL/port selections before committing any state.
- [x] Partial valid configuration never signals complete until all required fields arrive.
- [x] Short output and invalid messages preserve the previous configuration.
### Non-goals
- M5 trigger handling, live sockets, HDCP, all WFD profiles, playback integration.
### Edge Cases
- Embedded NUL, duplicate keys, unsupported formats, changed destination, wrong port, repeated selection.
## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Scope | Generic configurable engine or fixed fixture | Small explicit laboratory profile in tools | User approved isolated staged work |
| State | Incremental in-place writes or candidate copy | Candidate copy committed after successful serialization | Required atomic validation |
## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| 1 | steps/step-1.md | COMPLETED | M3/M4 core and black-box tests |
## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Test | make -C tools/miracast_protocol test sanitize | Existing target | yes |
| Compile | aarch64-none-elf-gcc -std=c11 -Wall -Wextra -Werror -Isource -c | SDK | yes |
| Hygiene | git diff --check | Git | yes |
## Context & Learnings
### Key Decisions
- Reuse parsed requests and standalone response decoder; leave production unchanged.
### Gotchas & Warnings
- Source format choices are not hardware-validation evidence. No readiness for playback is implied.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| tools/miracast_protocol | Existing offline handshake and test harness | Read |
| docs/miracast-control-development.md | Staged contract | Read |
### Verified Facts
- AOSP sendM4 sends video/audio, presentation URL and previously supplied client RTP ports.
- libwds messages_rules describes fixed-width video/audio fields and two presentation URLs.
- Existing production parser supplies bounded, NUL-terminated headers and body length.
## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-16 | 1 | Start M3/M4 laboratory profile after c93f9bc. |
| 2026-10-16 | 1 | Host, ASan/UBSan and devkitA64 compilation passed. Production diff empty. Restricted profile and incomplete runtime gates documented. |
