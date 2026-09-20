# Plan: AirPlay Logical Session Media Retention

> Status: COMPLETED
> Created: 2026-08-24
> Last Updated: 2026-08-24

## Goal
Keep an AirPlay HTTP logical session addressable across TCP reconnects while it owns active media, then reclaim it immediately after both media ownership and connection references are gone.

## Assumptions
- One active AirPlay URL-video owner is intentional because NX-Cast has one renderer.
- `X-Apple-Session-ID` remains the only cross-connection identity for AirPlay HTTP and reverse channels.
- RAOP connections remain unbound to HTTP logical sessions.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] Closing the final TCP connection does not delete a logical session retained by active media.
- [x] A new connection with the same `X-Apple-Session-ID` reattaches to the retained logical ID.
- [x] Releasing media removes a detached logical session immediately.
- [x] Connected sessions are not removed by media release, and RAOP remains independent.
- [x] Focused and full AirPlay tests pass.

### Non-goals
- Fixed disconnect grace timers, peer-IP association, multiple concurrent URL-video renderers, or changes to AirPlay discovery and pairing.

### Edge Cases
- Repeated retain/release calls are reference-counted safely; stale release cannot remove a session with live connections; shutdown after receiver teardown remains harmless.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Detached logical-session lifetime | Immediate deletion, fixed timer, permanent cache, explicit media retention | Explicit media retention reference tied to URL-video ownership | yes, user requested the protocol-layered model be implemented |
| Cross-connection identity | Peer IP, Apple session header | Exact `X-Apple-Session-ID` only | yes, explicitly confirmed in preceding discussion |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add and test the logical-session media retention contract. |
| Step 2 | `steps/step-2.md` | COMPLETED | Wire URL-video ownership to retention and run integration validation. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Focused session test | `make test-airplay-session` | `makefile` target | yes |
| Full AirPlay suite | `make test-airplay` | `makefile` target used by CI | yes |
| Switch build | `make dev-build` | repository build target | yes |
| Patch hygiene | `git diff --check` | repository workflow | yes |

## Context & Learnings
### Key Decisions
- Logical session connection references and media references are independent ownership dimensions.
- Existing remote-video ownership callbacks are reused to avoid introducing a second lifecycle state machine.

### Gotchas & Warnings
- The worktree contains substantial existing AirPlay and UI changes; edits must not overwrite unrelated work.
- Receiver shutdown currently precedes remote-video destruction, so media release after receiver teardown must be a safe no-op.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/protocol/airplay/protocol/logical_session.[ch]` | Connection-to-control-session identity and reference lifetime | targeted read found immediate deletion at zero connection references |
| `scripts/test_airplay_session.c` | Focused host regression coverage | targeted read found RAOP independence and Apple-session association tests |
| `source/protocol/airplay/integration.c` | URL-video ownership claim/release boundary | targeted read found reusable `integration_remote_claim/release` callbacks |
| `source/protocol/airplay/receiver.[ch]` | Owns the logical-session manager and exposes receiver lifecycle | targeted read found the singleton manager creation/destruction boundary |
| `source/protocol/airplay/media/remote_video.c` | URL playback generation and owner lifecycle | targeted read confirmed claim/release is balanced across play, stop, failure, replacement, and destruction paths |
| `scripts/test_airplay_remote_video.c` | Remote-video takeover regression coverage | impact review found cross-protocol release clears the global lease without clearing remote-video owner state |

### Verified Facts
- Before this change, a logical entry was cleared as soon as its connection count reached zero even when active URL media kept playing — verified by the pre-change read and focused regression test, 2026-08-24.
- Direct URL playback claims ownership once, reuses it for same-session replacement, and releases it on stop, failure, replacement, or destruction — verified by targeted read of `remote_video.c` and `integration.c`, 2026-08-24.
- RAOP requests without an Apple session header remain unbound and use their connection ID locally — verified by `logical_session.c` and `test_airplay_session.c`, 2026-08-24.
- Cross-protocol takeover releases the coordinator lease directly and previously left `AirPlayRemoteVideo.owner_claimed` active, preventing a later same-session `/play` from claiming a fresh lease — verified by targeted reads of `integration.c` and `remote_video.c`, 2026-08-24.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-24 | Step 1 | Added mutex-protected media references; detached active sessions now retain identity until the final media release, with reconnect and reclamation tests passing. |
| 2026-08-24 | Step 2 | Wired retention to URL-video ownership, cleared remote-video state during cross-protocol takeover, added trace boundaries, and passed full host and Switch builds. |
