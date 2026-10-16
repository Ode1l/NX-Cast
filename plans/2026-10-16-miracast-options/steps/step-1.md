# Step 1: OPTIONS Exchange

> Status: COMPLETED
> Created: 2026-10-16

## Goal
Provide a bounded, host-testable M1/M2 exchange with explicit lifecycle.

## Prerequisites
- Existing common RTSP request parser and isolated Miracast branch.
- User authorized offline work while hardware is unavailable.

## Deliverables
- C handshake core, focused host tests, protocol flow and handoff documentation.

## Plan
- [x] Add tools/miracast_protocol core and test runner reusing RTSP request parser.
- [x] Document exact support, reference and missing transport/media stages.
- [x] Run host and sanitizer tests, verify no production changes.

## Quality Checklist
- [x] Evidence-before-edit: common parser header and platform plan read; isolated callers only.
- [x] Reuse: common request parser; no new generic event bus or worker.
- [x] Contract: caller owns transport, response parsing and monotonic clock.
- [x] Risk: untrusted fields, partial writes, stale responses and time overflow.
- [x] Mitigation: bounded buffers and state tests.

## Validation Checklist
- [x] Strict compiler warnings and git diff --check pass.
- [x] devkitA64 compiles options.c with -std=c11 -Wall -Wextra -Werror.
- [x] git diff --exit-code HEAD -- source makefile .github scripts/package_release.sh is empty.

## Test Checklist
- [x] Host black-box transition tests and ASan/UBSan pass.

## Implementation Notes
Implemented only M1/M2 core, not a runnable receiver. Tests cover wire output,
partial M1 input, short buffers, separate CSeq spaces, malformed requests,
unsupported extensions, response rejection, timeout boundary/overflow,
independent connections and closure. M2 wire parsing remains a documented future
adapter, not claimed complete. Device and full protocol validation deferred.

## Files Changed
- tools/miracast_protocol/options.h
- tools/miracast_protocol/options.c
- tools/miracast_protocol/test_options.c
- tools/miracast_protocol/Makefile
- tools/miracast_protocol/README.md
- docs/miracast-control-development.md
- docs/miracast-platform-feasibility.md
- plans/2026-10-16-miracast-options/plan.md
- plans/2026-10-16-miracast-options/steps/step-1.md
