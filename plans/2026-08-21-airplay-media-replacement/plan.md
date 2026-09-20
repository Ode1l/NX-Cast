# Plan: AirPlay Media Replacement Lifecycle

> Status: COMPLETED
> Created: 2026-08-21
> Last Updated: 2026-08-21

## Goal
Make AirPlay direct-video sessions preserve media across control disconnects while reliably replacing a failed or active URL when a new `/play` transaction arrives.

## Assumptions
- Authenticated AirPlay requests reaching `AirPlayRemoteVideo` may replace a detached direct-video transaction without application-specific checks.
- Reverse-HLS playlist rewriting remains outside this task; only its disconnect/failure lifecycle must remain correct.
- Existing dirty worktree changes are user work and must be preserved without creating a protective WIP commit.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [ ] A second direct `/play` on the same control session loads the new URL without leaking or reacquiring the existing media lease.
- [ ] A failed direct `/play` leaves the session able to submit and play a different URL.
- [ ] Closing an active direct-video control connection does not stop playback; a later `/play` from a new session replaces it deterministically.
- [ ] A stale close or failure from an older transaction cannot stop or clear the replacement transaction.

### Non-goals
- Reworking reverse-HLS playlist parsing or implementing application-specific Bilibili behavior.
- Changing DLNA, IPTV, mirror, audio, or player rendering behavior.

### Edge Cases
- Same-session replacement, detached-session takeover, load failure followed by retry, pending-HLS disconnect, and stale close after replacement.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Disconnect semantics | Generic delayed Stop vs mode-specific lifecycle | Active direct media becomes detached and keeps its lease; pending negotiation is cancelled | yes |
| New `/play` semantics | Reject while old media exists vs deterministic replacement | Reuse the lease for same-session replacement; release/claim for detached-session takeover | yes |
| Compatibility strategy | App-name branches vs protocol state invariants | Protocol mode/session/generation rules only | yes |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Protect and implement direct-media URL replacement and detached takeover semantics. |
| Step 2 | `steps/step-2.md` | COMPLETED | Remove integration-level delayed Stop policy and validate the complete lifecycle. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Focused test | `make build/tests/test_airplay_remote_video && build/tests/test_airplay_remote_video` | `makefile` target and existing host test | yes |
| AirPlay regression | `make test-airplay` | `makefile` | yes |
| Build | `make full-trace-build -j4` | existing project build target | yes |
| Diff hygiene | `git diff --check` | git | yes |

## Context & Learnings
### Key Decisions
- Media transaction state and control-connection attachment are separate facts; socket closure is not a media Stop command.
- URL replacement is committed through the existing player `OPEN` command and coordinator lease rather than a second global state machine.

### Gotchas & Warnings
- `source/protocol/airplay/media/remote_video.c` and integration files already contain uncommitted work; edits must preserve those changes.
- Player commands are asynchronous, so generation/session ownership must reject stale cleanup rather than relying on callback timing.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/protocol/airplay/media/remote_video.c` | AirPlay `/play`, `/stop`, action, and disconnect state machine | `read` lines 390-712 and 947-1113 |
| `source/protocol/airplay/media/remote_video.h` | Remote-video callback contract | `read` full header |
| `source/protocol/airplay/integration.c` | Coordinator lease and detached cleanup policy | `read` lines 1-260 and 620-710 |
| `scripts/test_airplay_remote_video.c` | Host-side lifecycle regression tests | `read` lines 300-510 and 650-780 |
| `source/app/protocol_coordinator.c` | Existing lease generation and stale-lease validation | `read` lines 1484-1716 |

### Verified Facts
- Same-session `/play` currently overwrites `owner_claimed` and calls `claim_owner` again instead of treating the request as replacement — verified by `read` of `remote_video.c:503-588`, 2026-08-21.
- Active session close currently clears remote state, increments generation, and schedules an integration-layer delayed Stop — verified by `read` of `remote_video.c:1081-1112` and `integration.c:176-202,887-924`, 2026-08-21.
- Coordinator claims produce generation-scoped leases and reject stale leases, so it can remain the ownership authority — verified by `read` of `protocol_coordinator.c:1484-1683`, 2026-08-21.
- Existing host tests already provide load/claim/release/stop counters suitable for replacement lifecycle coverage — verified by `read` of `test_airplay_remote_video.c`, 2026-08-21.
- Same-session replacement, failure retry, detached takeover, and stale close isolation pass in the remote-video host test — verified by `make test-airplay`, 2026-08-21.
- The generic detached cleanup timer and empty tick interface have no remaining source references — verified by `rg remote_detach|detached-cleanup|airplay_integration_tick`, 2026-08-21.
- The complete Switch full-trace target links `NX-Cast.nro` with the new lifecycle — verified by `make full-trace-build -j4`, 2026-08-21.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-21 | Step 1 | Added replacement/retry/takeover tests and separated active media from control attachment in `remote_video`. |
| 2026-08-21 | Step 2 | Removed delayed detached Stop policy, made release callbacks release-only, removed dead tick API, and passed host/Switch validation. |
