# Step 1: Parameter Queries And Selection

> Status: COMPLETED
> Created: 2026-10-16

## Goal
Produce validated M3 replies and atomic M4 snapshots without I/O.
## Prerequisites
- OPTIONS core and response decoder pass host tests; user authorized continuation.
## Deliverables
- C parameter handler, focused tests and explicit profile/limitations documentation.
## Plan
- [x] Implement query serialization and candidate-based selection.
- [x] Exercise full and partial negotiation plus failures through wire messages.
- [x] Validate host/sanitizer/SDK builds and record remaining gates.
## Quality Checklist
- [x] Existing request/response APIs and references read; isolated scope only.
- [x] Reuse tests and parser; no new framework, sockets or workers.
- [x] Contract: validated parsed requests in, RTSP response bytes and atomic snapshot out.
- [x] Risks: input bounds, forged destinations, partial state and capability overclaim.
- [x] Mitigations verified by tests and lab-only placement.
## Validation Checklist
- [x] Strict host and SDK compilation, diff hygiene pass.
## Test Checklist
- [x] make -C tools/miracast_protocol test sanitize passes.
## Implementation Notes
Implemented no-I/O M3/M4 subset. Atomic candidate commits follow successful
serialization, not actual transmission; transport send failure must close the
future session. Tests cover full/partial/repeated negotiation, rollback after a
valid preceding field, bounds, content type, unknown keys and format selection.
Port values are compared numerically, accepting equivalent whitespace/leading
zeros. No protocol code copied wholesale. M0 remains unverified, production
integration disabled, and full WFD parameter-error-body handling remains future work.
## Files Changed
- tools/miracast_protocol/parameters.h
- tools/miracast_protocol/parameters.c
- tools/miracast_protocol/test_parameters.c
- tools/miracast_protocol/Makefile
- tools/miracast_protocol/test_options.c
- tools/miracast_protocol/README.md
- docs/miracast-control-development.md
- plans/2026-10-16-miracast-parameters/plan.md
- plans/2026-10-16-miracast-parameters/steps/step-1.md
