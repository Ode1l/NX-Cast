# Plan: Harden AirPlay Ownership And Callback Semantics

> Status: COMPLETED
> Created: 2026-08-17
> Last Updated: 2026-08-17

## Goal
Eliminate the AirPlay ownership races, null-claim and TOCTOU paths, propagate audio-record failures, and make the new state invariants explicit without regressing direct URL, audio-first, or DLNA/IPTV behavior.

## Assumptions
- Commit `019bdae` is the protected baseline for the previous AirPlay boundary work.
- The existing protocol coordinator and player actor remain the global ownership mechanism.
- Direct URL playback without a `claim_owner` callback remains allowed for existing smoke/test configurations.
- Reverse HLS requires a valid owner-claim callback because it must mutate global player ownership.
- Real iPhone compatibility remains outside automated validation.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] `ACTION_READY` transitions session state to active before `load()`/`play()` and no cleanup path can leak an owner lease.
- [x] Reverse HLS fails closed when `claim_owner` is unavailable.
- [x] Pending scrub decisions are made under one remote-video state lock without TOCTOU.
- [x] Audio-only record callback failures are returned and observed instead of silently acknowledged.
- [x] `load_queued` and `play_queued` are renamed to explicit player-queue names.
- [x] Focused host tests, strict trace build, and release build pass.
- [x] DLNA and IPTV behavior remains unchanged.

### Non-goals
- Changing UxPlay reference behavior or adding arbitrary `/action` validation relaxation.
- Adding a new global ownership framework or player backend.
- Implementing real-device automation.

### Edge Cases
- `/action` ready races with `/stop` or logical-session close.
- `claim_owner` is NULL for a reverse HLS session.
- Pending scrub races with action-ready.
- Direct URL optional claim races with `/stop` or logical-session close.
- Audio-only record callback no-ops because runtime state is stale.
- Legacy direct playback without an owner callback.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Remote session state | Two booleans vs explicit enum | Private enum `IDLE/PENDING_HLS/ACTIVE` plus `owner_claimed` | yes |
| Ownership transition | Claim then load then promote vs promote state before load | Claim, promote state before player I/O, then load/play | yes |
| Reverse claim contract | Allow NULL callback vs fail closed | Fail closed with 503 when `claim_owner` is NULL | yes |
| Direct claim race | Trust claim result vs revalidate generation | Revalidate generation after claim and hold the state lock through load/play | yes |
| Audio record API | Void callback vs boolean result | Boolean callback and boolean runtime API | yes |
| Queue names | Keep short names vs explicit player names | `player_load_queued` / `player_play_queued` | yes |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Replace remote-video booleans with an explicit state model and fix action-ready ownership transition. |
| Step 2 | `steps/step-2.md` | COMPLETED | Make reverse HLS claim-required and remove scrub TOCTOU. |
| Step 3 | `steps/step-3.md` | COMPLETED | Propagate audio-only record failures through handler and integration boundaries. |
| Step 4 | `steps/step-4.md` | COMPLETED | Rename runtime queue flags and add targeted regression coverage. |
| Step 5 | `steps/step-5.md` | COMPLETED | Update contracts and run full host/Switch validation. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Diff hygiene | `git diff --check` | Git worktree convention | yes |
| Focused host suite | `make test-airplay` | `makefile` | yes |
| Strict trace build | `make full-trace-build BUILD_JOBS=4` | `makefile` | yes |
| Release build | `make release-build RELEASE_JOBS=4` | `makefile` | yes |

## Context & Learnings
### Key Decisions
- State ownership must be explicit because cleanup behavior differs between pending negotiation and active playback.
- Player I/O occurs after the state is promoted so teardown can always stop and release a claimed owner.
- Direct optional claims are revalidated by generation after the callback returns; load/play then run under the state lock.
- Keep direct URL behavior compatible with the existing receiver smoke server.
### Gotchas & Warnings
- `claim_succeeded` must represent an actual callback result, not the absence of a callback.
- Audio record failures should not be converted into a misleading `Waiting for AirPlay video` status.
- Rename queue fields across all uses, including tests and diagnostics, to avoid split-brain state names.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/protocol/airplay/media/remote_video.c` | Remote session state and ownership transition | Read current `handle_action`, `handle_stop`, and session cleanup paths |
| `scripts/test_airplay_remote_video.c` | Ownership and pending-control regression | Read current reverse HLS deferral test |
| `source/protocol/airplay/protocol/handlers.c` | RTSP record callback boundary | Read current audio/mirror setup handling |
| `source/protocol/airplay/integration.c` | Audio record integration and status | Read `integration_audio_record` |
| `source/protocol/airplay/media/mirror_runtime.c` | Audio-only and mirror queue state | Read `record_audio`, `record`, `open`, `stop` |
| `scripts/test_airplay_handlers.c` | Handler callback regression | Read current fake callbacks |
| `scripts/test_airplay_mirror_runtime.c` | Runtime promotion regression | Read current audio-first test |
| `makefile` | Validation targets | `rg test-airplay makefile` |
| `docs/AIRPLAY_DEVELOPMENT.md` | Development semantics and validation coverage | Read full document before contract update |
| `docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md` | Route and ownership compatibility contract | Read full document before contract update |
### Verified Facts
- Mirror runtime private flags are now `player_load_queued` and `player_play_queued`; no old names remain — verified by `rg` on 2026-08-17.
- Audio-only record callbacks and runtime API now return booleans; handler setup/record returns 461 on failure and integration reports an error status — verified by handler/runtime tests on 2026-08-17.
- Reverse HLS ready now returns 503 without player mutation when `claim_owner` is absent; pending scrub is decided under one remote mutex section — verified by source read and new tests on 2026-08-17.
- Remote-video sessions now use one enum state and `owner_claimed`; action-ready promotes state before player I/O and cleanup releases only a claimed owner — verified by source read and updated tests on 2026-08-17.
- Current `ACTION_READY` claims ownership before promoting `hls_pending` to `active` — verified by reading `remote_video.c` on 2026-08-17.
- Current scrub uses `session_hls_pending()` then a separate lock for mutation — verified by reading `remote_video.c` on 2026-08-17.
- `claim_owner` is not mandatory in `airplay_remote_video_create` and is omitted by the receiver smoke server — verified by reading creation validation and smoke fixture on 2026-08-17.
- Audio record callback is currently void and silently ignores runtime no-op results — verified by reading handler/runtime/integration code on 2026-08-17.
- `make test-airplay` is the project’s focused host validation target — verified by `rg test-airplay makefile` on 2026-08-17.
- Direct URL `/play` revalidates generation after an optional claim and holds the state lock through load/play; claim-time stop releases the owner and returns 409 — verified by a new deterministic regression test on 2026-08-17.
- Both Switch builds passed after the final direct-claim race fix — verified with `full-trace-build` and `release-build` on 2026-08-17.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-17 | Step 1 | Replaced remote-video state booleans with an explicit state model and closed the action-ready promotion window. |
| 2026-08-17 | Step 2 | Made reverse claim fail closed and removed scrub’s unlocked second state check. |
| 2026-08-17 | Step 3 | Propagated audio-only record failures and added handler/runtime failure coverage. |
| 2026-08-17 | Step 4 | Renamed runtime queue flags to explicit player queue names. |
| 2026-08-17 | Step 5 | Updated contracts, closed the direct-claim/stop race found in final reflection, and passed host plus both Switch validations. |
