# Plan: Correct Detached AirPlay Owner Takeover

> Status: COMPLETED
> Created: 2026-08-17
> Last Updated: 2026-08-17

## Goal
Keep AirPlay playback alive after final logical-connection close while adding a synchronous cross-protocol cleanup path so DLNA or IPTV can replace a retained AirPlay owner instead of being blocked by exclusive media resources.

## Assumptions
- The completed FCUP status and logical-close detachment fixes remain correct and must not be reverted.
- An iPhone may close HTTP/RTSP control sockets before the first frame while direct URL playback must continue.
- A cross-protocol `protocol_coordinator_media_begin()` call is an explicit takeover action.
- No timer-based disconnect grace period is needed; cleanup is event-driven by explicit `/stop`, shutdown, natural playback end, or another protocol claim.
- Existing same-resource AirPlay replacement semantics remain unchanged.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [ ] Final logical connection close still detaches remote-video control state without stopping or releasing active AirPlay playback.
- [ ] A subsequent DLNA or IPTV claim synchronously stops the retained AirPlay player and releases its coordinator owner before the new claim succeeds.
- [ ] A failed or stale AirPlay takeover callback rejects the new claim without corrupting either owner.
- [ ] Same-resource AirPlay replacement does not unnecessarily invoke the takeover callback.
- [ ] Host AirPlay tests, strict trace build, and release build pass.

### Non-goals
- Adding a timer-based “wait for reconnect” state machine.
- Changing Control Center audio-only/mirror negotiation.
- Relaxing cross-resource exclusivity for all protocol pairs.
- Reverting the advisory FCUP status or logical-close detachment fixes.

### Edge Cases
- Previous owner is AirPlay remote video, AirPlay mirror, or already absent.
- Previous owner is DLNA/IPTV when a new AirPlay claim arrives.
- Callback releases a stale generation while a newer owner appears concurrently.
- Explicit `/stop`, application shutdown, or natural end-file races with a takeover.
- Same AirPlay logical session sends a new `/play` after the previous controls close.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Detached playback cleanup | Timer after close vs synchronous takeover | Keep playback after close; release only on explicit end, shutdown, or new protocol claim | yes |
| Coordinator takeover contract | Generic owner callback vs AirPlay-specific callback | AirPlay-specific `airplay_release_active_media` callback in `ProtocolCoordinatorOperations` | yes |
| Release ordering | Queue release vs synchronous ownership release | Submit player stop, synchronously release coordinator lease, then clear AirPlay integration lease | yes |
| Remote-video state modeling | Add `DETACHED_PLAYBACK` state vs retain current `IDLE` state | Keep current state; retained ownership remains an integration/coordinator concern | yes |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add and test the coordinator takeover callback contract. |
| Step 2 | `steps/step-2.md` | COMPLETED | Implement synchronous AirPlay active-media release and wire it into the application. |
| Step 3 | `steps/step-3.md` | COMPLETED | Add takeover race and lifecycle regression coverage. |
| Step 4 | `steps/step-4.md` | COMPLETED | Update protocol lifecycle documentation. |
| Step 5 | `steps/step-5.md` | COMPLETED | Run host, full-trace, and release validation. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Diff hygiene | `git diff --check` | Git worktree convention | yes |
| Host regression suite | `make test-airplay` | `makefile` | yes |
| Strict trace build | `make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1` | `makefile` | yes |
| Release build | `make release-build RELEASE_JOBS=4` | `makefile` | yes |

## Context & Learnings
### Key Decisions
- AirPlay control sockets and AirPlay player ownership have different lifetimes.
- Cross-resource takeover is the missing cleanup boundary for a retained AirPlay owner.
### Gotchas & Warnings
- Do not call the takeover callback while holding the coordinator ownership transition lock.
- Re-read the current owner after the callback returns because another claim may have raced with cleanup.
- Player stop submission is asynchronous; coordinator ownership release must be synchronous so the new claim can proceed deterministically.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/app/protocol_coordinator.h` | Coordinator operations and media transaction contract | Read `ProtocolCoordinatorOperations` |
| `source/app/protocol_coordinator.c` | Cross-resource claim rejection path | Read `protocol_coordinator_media_begin` |
| `scripts/test_protocol_coordinator.c` | Coordinator takeover tests and fake operations | Read existing exclusive-owner tests |
| `source/protocol/airplay/integration.h` | Public AirPlay integration cleanup API | Read current stop APIs |
| `source/protocol/airplay/integration.c` | Retained remote/mirror lease cleanup | Read `stop_active_media` and `stop` |
| `source/main.c` | Coordinator operation wiring and UI stop path | Read protocol operation setup and return-home |
| `source/protocol/airplay/media/remote_video.c` | Detached logical-close semantics | Read `session_closed` |

### Verified Facts
- `protocol_coordinator_media_begin()` currently rejects any cross-resource claim while another media owner exists — verified by reading `source/app/protocol_coordinator.c` lines 1478-1494.
- The final logical connection close now clears remote-video control state without calling player stop/release — verified by reading `source/protocol/airplay/media/remote_video.c` lines 1081-1107.
- `airplay_integration_stop_active_media()` currently submits player commands asynchronously but does not synchronously release the coordinator lease itself — verified by reading `source/protocol/airplay/integration.c` lines 752-771.
- The UI return-home path separately calls `airplay_integration_stop_active_media()` and then `protocol_coordinator_media_release()` — verified by reading `source/main.c` lines 221-230.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-17 | 1 | Added coordinator takeover callback and host contract tests; cross-resource AirPlay takeover is now synchronously revalidated. |
| 2026-08-17 | 2 | Added AirPlay integration release for exact remote/mirror leases and wired it into the Switch protocol operations. |
| 2026-08-17 | 3 | Added remote-video same-session re-entry, mirror takeover, and stale-release race coverage. |
| 2026-08-17 | 4 | Documented explicit cross-protocol takeover and retained detached playback semantics. |
| 2026-08-17 | 5 | Passed host, trace, and release validation; rebuilt the final trace NRO. |
