# Step 1: Response Wire Adapter

> Status: COMPLETED
> Created: 2026-10-16

## Goal
Decode complete response bytes before advancing OPTIONS.
## Prerequisites
- Previous OPTIONS checkpoint b769e55; user authorized continuation.
## Deliverables
- Isolated response decoder, adapter, focused byte-level tests and updated roadmap.
## Plan
- [x] Implement bounded decoder and options adapter.
- [x] Test split/concatenated messages, invalid frames and state transitions.
- [x] Run sanitizers and cross-compile; update documentation.
## Quality Checklist
- [x] Existing parser and call contract read; changes isolated to tools/docs.
- [x] Reuse shared limits and request parser without changing production code.
- [x] Contract: complete/error/incomplete, unchanged output on non-success.
- [x] Risks: bounds, overflow, ambiguous framing and borrowed lifetime.
- [x] Mitigate with strict limits and black-box tests.
## Validation Checklist
- [x] Host and cross-compilation warnings clean; diff check clean.
## Test Checklist
- [x] make -C tools/miracast_protocol test sanitize passes.
## Implementation Notes
Added allocation-free response view and byte adapter. Tests cover all prefixes,
binary bodies, malformed/duplicate critical fields, numeric/header limits,
bounded repeated Public merging, stale replies and M2 followed by M3.
No device validation, partial socket writes or live transport is claimed.
Future capability handling must not mistake OPTIONS completion for media readiness.
## Files Changed
- tools/miracast_protocol/response.h
- tools/miracast_protocol/response.c
- tools/miracast_protocol/options.h
- tools/miracast_protocol/options.c
- tools/miracast_protocol/test_response.c
- tools/miracast_protocol/test_options.c
- tools/miracast_protocol/Makefile
- tools/miracast_protocol/README.md
- docs/miracast-control-development.md
- plans/2026-10-16-miracast-response/plan.md
- plans/2026-10-16-miracast-response/steps/step-1.md
