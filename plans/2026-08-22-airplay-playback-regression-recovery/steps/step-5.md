# Step 5: Derive Sender Feedback From Session State

> Status: PENDING
> Created: 2026-08-22

## Goal
Emit generation-matched video events from authoritative transitions and validate protocol-neutral baselines.

## Prerequisites
- Step 4 completed with isolated transport, stable state, and validated media.
- Files: reverse writer, integration callbacks, tests, and AirPlay docs.
- Design: feedback projects state; peer close changes attachment only.

## Deliverables
- Loading, playing, paused, failed, and stopped feedback follows transitions.
- TLS Stop cancellation is classified separately from genuine transport failure.
- After this step: full validation and manual direct/Reverse-HLS/DLNA/IPTV matrix are documented.

## Plan
- [ ] `read` reverse writer, snapshots, player events, and local event specification — map payloads.
- [ ] `edit` AirPlay tests — add state-event, detached, cancellation, and replacement transcripts.
- [ ] `edit` AirPlay server/integration — serialize bounded generation-checked events without socket-layer state mutation.
- [ ] `edit` TLS diagnostics — classify operation and cancellation before failure transition.
- [ ] `edit` AirPlay docs — record invariants and application-neutral manual matrix.
- [ ] `bash` `make test-airplay && make test-protocol-coordinator && make full-trace-build BUILD_JOBS=4 && git diff --check` — expect all pass.

## Quality Checklist
- [ ] Evidence-before-edit: read reverse/state/player/spec boundaries and run full validation
- [ ] Existing pattern / reuse checked: use current reverse registry and snapshots
- [ ] Contract understood: a transition produces at most one matching event
- [ ] Risk reviewed: socket race, stale feedback, false failure, blocked worker
- [ ] Mitigation recorded: serialized writes, timeout, generation guard, transcript tests

## Validation Checklist
- [ ] `make full-trace-build BUILD_JOBS=4` exits 0
- [ ] `git diff --check` exits 0

## Test Checklist
- [ ] `make test-airplay && make test-protocol-coordinator` — all pass
- [ ] Manual matrix — direct URL, HLS variants, replacement, Stop during TLS, DLNA, IPTV

## Implementation Notes
Pending.

## Files Changed
Pending.

