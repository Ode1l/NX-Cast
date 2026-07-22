# Step 1: Coordinator Suspension Policies

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Make ownership-driven and playback-driven discovery suspension explicit coordinator policies and protect both contracts with host tests.

## Prerequisites
- User confirmed ID 11 must remain ownership-driven and ID 12 must be player-state-driven.
- Files to modify: `source/app/protocol_coordinator.h`, `source/app/protocol_coordinator.c`, `scripts/test_protocol_coordinator.c`.

## Deliverables
- Public none/ownership/playback suspension policy and a playback activity update API.
- Existing ownership behavior only runs in ownership policy; playback updates only run in playback policy.
- After this step: focused coordinator tests pass for both policies.

## Plan
- [x] `edit` `scripts/test_protocol_coordinator.c` — add a playback-policy test proving claims do not suspend, active/inactive edges do, stopped-like inactivity resumes with ownership retained, and repeats are idempotent.
- [x] `edit` `source/app/protocol_coordinator.h` — add the policy type/config field and playback activity API.
- [x] `edit` `source/app/protocol_coordinator.c` — store/validate the policy and gate ownership/playback suspension sources without changing callback locking.
- [x] `bash` focused coordinator target — all ownership and playback policy tests pass.

## Quality Checklist
- [x] Evidence-before-edit: coordinator/config/test targets read; all suspension callers searched; focused validation command known.
- [x] Existing pattern / reuse checked: reuse callback, snapshot, revision, idempotence, and fake runtime events.
- [x] Contract understood: policy chooses one suspension source; callback remains atomic/nonblocking and outside coordinator lock.
- [x] Risk reviewed: cross-policy leakage, duplicate callbacks, stop/reset behavior, retained ownership.
- [x] Mitigation recorded: explicit enum validation and focused edge tests.

## Validation Checklist
- [x] Header and implementation signatures match.
- [x] `git diff --check` exits 0 (only Git's existing LF-to-CRLF notices were printed).

## Test Checklist
- [x] `make PORTLIBS_PREFIX=/opt/devkitpro/portlibs/switch PROTOCOL_COORDINATOR_TEST_BIN=/tmp/nxcast-test-protocol-coordinator.exe test-protocol-coordinator` exits 0.

## Implementation Notes
- Added a validated policy enum to the coordinator config. Policies that can suspend require a nonblocking callback.
- Ownership claim/release/tick paths now affect discovery only under the ownership policy; playback updates affect it only under the playback policy.
- Callback invocation remains outside the coordinator mutex. Repeated activity values and ownership takeovers are idempotent, and stop forces one final resume edge.
- Test-first compile failed on the deliberately missing enum/API, then passed after implementation.
- Whole-worktree stash/commit was not safe because the branch already contains the user's and prior diagnostic changes; patches were limited to the planned files.

## Files Changed
- `source/app/protocol_coordinator.h`
- `source/app/protocol_coordinator.c`
- `scripts/test_protocol_coordinator.c`
