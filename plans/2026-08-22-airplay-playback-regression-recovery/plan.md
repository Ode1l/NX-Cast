# Plan: AirPlay Playback Regression Recovery

> Status: ACTIVE
> Created: 2026-08-22
> Last Updated: 2026-08-22

## Goal
Restore AirPlay playback through one protocol-neutral media session state machine, isolated control/data planes, validated media descriptors, and generation-bound sender feedback without application- or host-specific branches.

## Assumptions
- DLNA and IPTV remain healthy and must not receive AirPlay-specific transport or demux behavior.
- Sender applications are interoperability fixtures only; production code may branch on protocol message, media format, transport result, or state, never application identity or CDN host.
- Advertisements and optional audio renditions are ordinary timeline/media inputs; filtering them is outside the receiver protocol contract.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [ ] An accepted video `/play` creates exactly one generation-bound session that transitions through preparing, active, detached, stopping, and idle without stale commands or double release.
- [ ] AirPlay control/reverse/RAOP connections cannot consume capacity reserved for loopback HLS requests.
- [ ] Media is validated and normalized by declared structure before demux selection; invalid input fails once instead of causing a retry storm.
- [ ] Direct URL, standard HLS, condensed fMP4 HLS, audio rendition, replacement, Stop, and peer-close paths use the same contracts.
- [ ] TLS results are associated with operation and generation; Stop cancellation is not reported as a playback root cause.
- [ ] Receiver loading, playing, paused, failed, and stopped feedback is emitted from authoritative state transitions.
- [ ] DLNA, IPTV, AirPlay, player callbacks, and transport workers mutate media-session authority only by submitting generation-bound events to one reducer.
- [ ] State transitions perform no player, network, logging, allocation, or blocking work while the coordinator mutex is held.
- [ ] Host suites and the Switch full-trace build pass without DLNA/IPTV regression.

### Non-goals
- Application-name, website, CDN-host, advertisement-duration, or URL-pattern behavior branches.
- Blocking or skipping advertisements.
- Replacing libmpv/FFmpeg or rewriting the entire application coordinator.

### Edge Cases
- Reverse/control connections may close while media remains active; detached is an explicit state, not implicit Stop.
- Replacement invalidates the old generation before creating the new one.
- Existing `#EXT-X-MAP` is not duplicated; ambiguous initialization semantics fail validation.
- Audio-only RAOP does not claim the video owner.
- Stop during DNS, TLS, playlist read, or segment read converges to idle without false failure feedback.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Recovery | Continue patching; revert all AirPlay; restore baseline then rebuild invariants | Restore the playable demux baseline, then change one invariant per step | yes, required by user |
| Transport | Increase shared slots; reserve disjoint slots; duplicate a local media server | Use one parser/listener with disjoint loopback-media and LAN-control pools | yes, removes contention without duplicating protocol code |
| Ownership | Claim after playlists; claim on first command; claim once on `/play` | Reserve once on accepted `/play` and retain that generation to terminal release | yes, matches the requested state-machine model |
| Compatibility | Sender-specific fixes; unconditional demux; descriptor validation | Parse and validate media structure, then select a supported demux path | yes, protocol-neutral solution required by user |
| Peer close | Stop; ignore; detached transition | Change attachment state independently while media stays owned | yes, separates connection and media lifetime |
| TLS | Treat every error as fatal; suppress all; classify operation/cancellation | Classify against operation and active generation | yes, evidence-driven diagnosis |
| Event architecture | Global event bus; new coordinator actor thread; one coordinator reducer with typed events | Add one protocol-neutral event dispatch path and pure reducer; reuse the existing player actor for player effects | yes, minimizes concurrency while enforcing one mutation boundary |
| Session model | One large enum; protocol-specific states; orthogonal dimensions | Keep lifecycle, playback, and control attachment as independent fields under one lease/generation | yes, avoids invalid Cartesian-state growth and sender-specific branches |
| Effect execution | Execute in callbacks; execute under coordinator lock; return transition result and retain existing adapters | Reducer returns a transition result; existing coordinator adapters execute effects after unlocking. Add an effect queue only when asynchronous orchestration requires it | yes, smallest implementation that preserves the boundary without a second framework |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Restore the known playable baseline and preserve causality diagnostics. |
| Step 2 | `steps/step-2.md` | COMPLETED | Isolate loopback HLS data-plane capacity from AirPlay control workers. |
| Step 3 | `steps/step-3.md` | COMPLETED | Enforce one generation-bound media session state machine. |
| Step 4 | `steps/step-4.md` | COMPLETED | Validate HLS normalization while retaining automatic demux selection. |
| Step 5 | `steps/step-5.md` | PENDING | Emit state-derived sender feedback and run the regression matrix. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Cache policy | `make test-player-cache-policy` | Existing target | yes |
| AirPlay suite | `make test-airplay` | Existing target | yes |
| Coordinator suite | `make test-protocol-coordinator` | Existing target | yes |
| Switch build | `make full-trace-build BUILD_JOBS=4` | Existing VS Code task | yes |
| Whitespace | `git diff --check` | Git built-in | yes |

## Context & Learnings
### Key Decisions
- The state machine owns session authority; transport capacity, descriptor validity, and TLS results are separate contracts that submit events to it.
- The coordinator is the only media-session state writer. Protocol adapters translate wire requests into events; the player actor and transport workers translate outcomes back into events.
- Events carry bounded scalar data and descriptor handles, never borrowed URL/body pointers. State changes are deterministic; I/O and blocking work are effects outside the reducer.
- Control and local media data planes never contend for one fixed pool.
- Sender names appear only in manual compatibility records.

### Gotchas & Warnings
- Signed URLs must not enter diagnostics or fixtures.
- The dirty worktree must not be reset or restored wholesale.
- Repeated demux errors amplify one descriptor defect plus one shared-capacity defect.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260822-152311.log` | Runtime evidence | `rg`/`sed` isolated direct playback, Reverse HLS failure, stale generations, local HTTP 503, and Stop-adjacent TLS errors. |
| `logs/run_nxlink-20260821-193121.log` | Playable comparison | Automatic demux rendered Reverse HLS before the latest regression. |
| `source/protocol/airplay/server.c` | Shared worker pool | `nl` verified four fixed slots and explicit 503 on exhaustion. |
| `source/protocol/airplay/server.h` | Capacity contract | `rg` verified `AIRPLAY_SERVER_MAX_CLIENTS` is four. |
| `source/player/cache_policy.c` | Demux policy | `rg` showed unconditional forcing for local `/airplay-hls/`. |
| `source/protocol/airplay/media/remote_hls.c` | Descriptor rewrite | `nl` showed URI expansion without initialization capability validation. |
| `source/protocol/airplay/media/remote_video.c` | Session state | `git diff` showed ownership deferred until the final HLS action. |
| `source/app/protocol_coordinator.c` | Media authority | Trace showed old-generation commands after cross-protocol release. |
| `source/app/protocol_coordinator.h` | Current coordinator contract | Source inspection found lease/generation and snapshot APIs, but no unified typed event dispatch API. |
| `source/player/core/media_actor.h` | Reusable concurrency pattern | Source inspection verified a bounded command queue, one executor, stale validation, synchronous wait, and health counters. |
| `source/player/core/session.c` | Player effect boundary | Source inspection verified public player commands are translated to the existing media actor instead of executing backend work on caller threads. |
| `../others/UxPlay-master/lib/http_handlers.h` | Protocol reference | Local source uses protocol messages rather than sender identity. |
| `../others/openairplay-master/documentation/Unofficial AirPlay Protocol Specification.html` | Event reference | Local documentation defines receiver video state events. |

### Verified Facts
- The AirPlay server has four slots and returns 503 when none is available — verified by source inspection, 2026-08-22.
- During failed Reverse HLS attempts, shared control/data concurrency caused six `HTTP error 503` events — verified by timestamp correlation, 2026-08-22.
- Forced HLS demux produced 910 missing-initialization errors and 454 unrecognized-format terminations without first frame — verified by counted logs, 2026-08-22.
- The only two TLS errors follow replacement Stop and return-home Stop; both end as `reason=stop error=success` — verified by contextual logs, 2026-08-22.
- SSL is therefore not the initial playback root cause; it is currently a cancellation-classification issue — verified by event ordering, 2026-08-22.
- A new Reverse HLS `/play` initially used the preceding token/generation and produced two stale rejections — verified by log ordering, 2026-08-22.
- Direct URL media reached H.264/AAC, nvtegra/HOS, and first frame before control peers closed — verified by log ordering, 2026-08-22.
- Media ownership is currently mutated through imperative coordinator calls from DLNA, IPTV, AirPlay integration, `main.c`, and playback observation — verified by `rg protocol_coordinator_media_`, 2026-08-22.
- AirPlay remote video also maintains a separate three-state session and invokes claim/release/stop callbacks from multiple branches — verified by source inspection of `remote_video.c`, 2026-08-22.
- The player layer already provides the queue/actor behavior needed for player side effects; creating a second generic event framework would duplicate working code — verified by `media_actor.h` and `session.c`, 2026-08-22.
- The protocol-neutral reducer now owns lifecycle, playback, control attachment, lease, and session revision; coordinator adapters synchronize legacy ownership and execute side effects outside the reducer lock — verified by source review and the coordinator black-box sequence, 2026-08-22.
- Full AirPlay host regression and the Switch full-trace NRO build pass with the new event path — verified by `make test-airplay` and `make full-trace-build BUILD_JOBS=4`, 2026-08-22.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-22 | Step 3 | Added the protocol-neutral media-session reducer, coordinator event dispatch, player-state and AirPlay adapters, state transition logging, and one black-box sequence test. |
| 2026-08-22 | Step 1 | Removed the loopback-HLS forced demux override while retaining cache policy and automatic-demux diagnostics. |
| 2026-08-22 | Step 2 | Partitioned the AirPlay worker pool into four LAN-control and four loopback-media slots; capacity traces now identify the exhausted pool. |
| 2026-08-22 | Step 4 | Confirmed the missing fMP4 initialization followed local 503 responses, retained automatic demux, and added a condensed zero-based initialization-range fixture. |
