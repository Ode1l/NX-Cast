# Plan: AirPlay General Session Fix

> Status: COMPLETE
> Created: 2026-09-02
> Last Updated: 2026-09-03

## Goal
Make AirPlay transport termination, HTTP video ownership, and displayed metadata behave consistently across senders without application-specific rules.

## Assumptions
- The sender completes pair verification on both RAOP and AirPlay HTTP connections before protected media control.
- A verified client Ed25519 public key is stable across connections belonging to one sender.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [ ] RAOP and HTTP connections remain separate sessions but can be correlated through verified sender identity.
- [ ] Media stops only after an associated explicit transport teardown and final video-control disconnect, in either event order.
- [ ] A transient or isolated HTTP/reverse disconnect retains media and reports detached control.
- [ ] Generic title fields in `/play` metadata are preferred over `clientProcName` without sender-specific logic.

### Non-goals
- Do not merge sessions by peer IP, add sender-specific behavior, or reinterpret `selectedMediaArray` language-selection data as a title.
- Do not change DLNA, IPTV, decoder, or cache behavior.

### Edge Cases
- HTTP controls may close before or after RAOP `TEARDOWN`.
- More than one verified sender may connect from the same network address.
- `/stop` may release media before a later transport teardown.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Cross-connection identity | peer IP; merge all IDs; verified pairing identity | Keep layered IDs and correlate with verified pairing identity | yes |
| Terminal rule | any disconnect; fixed timeout; teardown plus final control close | Require both protocol-level conditions in either order | yes |
| Title source | selectedMediaArray; app-specific parser; generic `/play` title fields | Generic `/play` title fields with clientProcName fallback | yes |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETE | Implement identity-correlated terminal lifecycle end to end. |
| Step 2 | `steps/step-2.md` | COMPLETE | Prefer generic AirPlay media titles while preserving fallback metadata. |
| Step 3 | `steps/step-3.md` | COMPLETE | Run focused and repository validation and document results. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Session tests | `make test-airplay-session` or discovered equivalent | `makefile`, scripts | yes |
| Handler tests | `make test-airplay-handlers` or discovered equivalent | `makefile`, scripts | yes |
| Remote video tests | `make test-airplay-remote-video` or discovered equivalent | `makefile`, scripts | yes |
| Host suite | existing aggregate host-test target | `makefile` | yes |
| Switch build | existing project build command when toolchain is available | `makefile` | yes |

## Context & Learnings
### Key Decisions
- Pairing identity correlates transport and media without collapsing their protocol-specific session IDs.
- `selectedMediaArray` carries media-selection options in UxPlay and is not treated as title metadata.
### Gotchas & Warnings
- Shutdown-time `Connection reset by peer` remains unrelated to playback termination.
- An audio-only RAOP teardown must not stop video until its HTTP/reverse control set has also closed.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/protocol/airplay/security/pairing.c` | verified sender identity source | stores verified `client_ed_public` per connection |
| `source/protocol/airplay/protocol/logical_session.c` | layered connection/media session registry | currently binds only `X-Apple-Session-ID` |
| `source/protocol/airplay/receiver.c` | protocol routing and close boundary | currently reports only final logical HTTP close |
| `source/protocol/airplay/media/remote_video.c` | media ownership and `/play` parsing | active close currently becomes detached |
| `scripts/test_airplay_session.c` | session behavior tests | currently asserts RAOP remains unbound |
| `../others/UxPlay-master/lib/raop.c` | reference connection classification | separates RAOP and AirPlay HTTP connections |
| `../others/UxPlay-master/lib/raop_handlers.h` | reference teardown semantics | parses transport teardown separately from HTTP `/stop` |

### Verified Facts
- NX-Cast currently stores the verified client Ed25519 public key but exposes only verification state and shared secret — verified by `rg` and `read`, 2026-09-02.
- HTTP `/play` and `/reverse` already share `X-Apple-Session-ID`; RAOP remains connection-local — verified by `logical_session.c` and tests, 2026-09-02.
- UxPlay classifies RAOP and AirPlay as separate connection types rather than merging them by IP — verified by `../others/UxPlay-master/lib/raop.c`, 2026-09-02.
- UxPlay interprets `selectedMediaArray` as language/media selection, not the content title — verified by `../others/UxPlay-master/lib/http_handlers.h`, 2026-09-02.
- Pairing verification now exposes a copied Ed25519 sender identity; the session manager uses it without merging RAOP and HTTP connection IDs — verified by focused AirPlay tests, 2026-09-03.
- Explicit RAOP teardown and final HTTP media-control close now form an order-independent terminal condition; isolated control close remains detached — verified by focused AirPlay tests, 2026-09-03.
- The repository AirPlay suite and Horizon build both exit successfully after the change — verified by `make test-airplay` and `make -j4`, 2026-09-03.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-03 | Step 1 | Added verified-identity correlation and order-independent terminal media release across pairing, logical sessions, receiver routing, and remote-video ownership. `make test-airplay` completed all test binaries successfully. |
| 2026-09-03 | Step 2 | Added bounded generic title selection for binary `/play` payloads with `clientProcName` fallback; focused remote-video tests pass. |
| 2026-09-03 | Step 3 | Reviewed the task diff and API usages, ran `git diff --check`, completed `make test-airplay`, and linked `NX-Cast.nro` with `make -j4`; all exited 0. |
