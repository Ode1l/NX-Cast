# Plan: Fix AirPlay Protocol Boundary Semantics

> Status: COMPLETED
> Created: 2026-08-16
> Last Updated: 2026-08-16

## Goal
Make AirPlay HLS action rejection observable and defer player ownership/loading until the sender has actually provided the media path, while separating audio-only setup from type-110 mirroring.

## Assumptions
- The current `airplay` branch checkpoint `a9a8e62` is the implementation baseline.
- Existing DLNA/IPTV behavior and the existing protocol coordinator must remain the global ownership mechanism.
- No real-device `/action` body is available yet, so this task must add diagnostics and preserve strict validation rather than arbitrarily relax protocol checks.
- UxPlay remains a behavior reference only; NX-Cast remains a native C implementation.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] `/action` failures expose a bounded, secret-free reason through the existing trace/logging policy.
- [x] Audio-only `SETUP` starts media ingress without claiming `airplay-mirror` or loading `airplay://mirror`.
- [x] Type-110 video promotes an existing audio bridge and only then claims mirror ownership and loads the player.
- [x] Reverse HLS does not claim `airplay-video` or issue player commands until `ACTION_READY`.
- [x] Focused host tests, `make test-airplay`, strict trace build, and release build pass.
- [x] DLNA and IPTV ownership tests remain unchanged and passing.

### Non-goals
- Relaxing `/action` validation before a real iPhone plist fixture is available.
- Implementing AirPlay 2 multi-room audio, music-player behavior, HEVC, AWDL, or commercial DRM.
- Replacing the protocol coordinator or player actor.

### Edge Cases
- `/action` arrives with a missing Content-Type, malformed binary plist, missing fields, mismatched request id, mismatched URL, non-2xx status, or invalid playlist.
- `RECORD` arrives after audio setup but before type-110 video.
- Type-110 arrives after audio-only recording and must replace the bridge exactly once.
- HLS negotiation receives control commands or teardown before `ACTION_READY`.
- A stale HLS generation must not claim a newer player lease.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Action diagnostics | Generic 400 vs explicit result enum | Add secret-free result enum while preserving HTTP status behavior | yes |
| Audio-first lifecycle | Load audio-only immediately vs wait for type-110 | Keep audio ingress active but defer mirror ownership and player load until type-110 | yes |
| HLS ownership timing | Claim on `/play` vs claim on `ACTION_READY` | Claim on `ACTION_READY`; negotiation state is session-local | yes |
| Validation surface | Change host test APIs freely vs keep compatibility wrappers | Update focused tests and callers in the same change | yes |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Clean the checkpoint and establish the current test baseline. |
| Step 2 | `steps/step-2.md` | COMPLETED | Add explicit HLS action rejection reasons and safe diagnostics. |
| Step 3 | `steps/step-3.md` | COMPLETED | Separate audio-only recording from mirror ownership/player load. |
| Step 4 | `steps/step-4.md` | COMPLETED | Defer Reverse HLS ownership/player mutation until action-ready. |
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
- Keep strict validation until a real device body proves which field must change.
- Treat audio-only and type-110 mirroring as separate media readiness states inside the existing mirror runtime.
- Defer Reverse HLS ownership to `ACTION_READY`, while keeping all HLS state session-scoped.
### Gotchas & Warnings
- `git diff --check` reports two trailing blank lines in the new FFmpeg install/verify scripts from the checkpoint.
- Matroska cannot add a video stream after an audio-only header has been written; late type-110 must replace the bridge generation.
- Logging must not include Apple Session IDs, complete URLs, plist bodies, or media payloads.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/protocol/airplay/media/remote_hls.c` | FCUP action parser and playlist rewrite | Read parser validation branches on 2026-08-16 |
| `source/protocol/airplay/media/remote_video.c` | Remote video ownership and route dispatch | Read `/play`, `/action`, `/rate`, `/stop` paths on 2026-08-16 |
| `source/protocol/airplay/protocol/handlers.c` | RTSP `SETUP`/`RECORD` state transitions | Read audio/mirror setup and record condition on 2026-08-16 |
| `source/protocol/airplay/media/mirror_runtime.c` | Audio/mirror bridge and player handoff | Read audio-open, record, and replace paths on 2026-08-16 |
| `source/protocol/airplay/integration.c` | Coordinator ownership glue | Read mirror claim and receiver callbacks on 2026-08-16 |
| `scripts/test_airplay_remote_hls.c` | FCUP action regression | Read action fixture builder and assertions |
| `scripts/test_airplay_remote_video.c` | Remote video ownership regression | Read direct playback dispatch test |
| `scripts/test_airplay_handlers.c` | RTSP setup/record regression | Read existing transcript coverage |
| `scripts/test_airplay_mirror_runtime.c` | Mirror runtime promotion regression | Read existing audio/replace tests |
| `makefile` | Host test and Switch build targets | `rg test-airplay makefile` |
### Verified Facts
- Strict trace and release Switch builds pass after the protocol boundary changes; release attests `airplay-muxer=1`, `ed25519=1`, `deko3d=1`, and `libmpv=1` — verified by local `make` commands on 2026-08-16.
- Reverse HLS `/play` now keeps a pending session state without claiming `airplay-video`; ownership and player load occur at `ACTION_READY`, with pending rate/scrub/playback-info handled without null-URL commands — verified by updated remote-video transcript tests on 2026-08-16.
- Audio-only `RECORD` now invokes a separate callback and does not claim `airplay-mirror` or load the player; type-110 later uses a normal LOAD after bridge promotion — verified by handler and mirror-runtime tests on 2026-08-16.
- `/action` now reports distinct safe reasons for content type, plist decode, shape, status, request id, URL/data, playlist, session/request/URL mismatch, rewrite, and bad state — verified by source read and updated host tests on 2026-08-16.
- `make test-airplay` passes on the checkpoint plus EOF cleanup — verified by local command on 2026-08-16.
- `git diff --check` passes after removing the two trailing blank lines — verified by local command on 2026-08-16.
- Current `/action` failure paths collapse to one boolean result and HTTP 400 — verified by reading `remote_video.c` and `remote_hls.c` on 2026-08-16.
- Current `RECORD` accepts `context->mirror_setup || context->audio_setup` and always calls `media_record_callback` — verified by reading `handlers.c` on 2026-08-16.
- Current mirror record path accepts `runtime->audio` and enqueues `AIRPLAY_RUNTIME_COMMAND_LOAD` — verified by reading `mirror_runtime.c` on 2026-08-16.
- Current `/play` claims `airplay-video` before the reverse HLS action completes — verified by reading `remote_video.c` and the latest hardware log on 2026-08-16.
- `make test-airplay` is the repository's focused AirPlay validation target — verified by `rg test-airplay makefile` on 2026-08-16.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-16 | Step 1 | Removed EOF blank-line warnings and confirmed the full AirPlay host suite passes at the new baseline. |
| 2026-08-16 | Step 2 | Added secret-free `/action` result reasons and expanded focused failure-path tests. |
| 2026-08-16 | Step 3 | Split audio-only ingress from mirror ownership/player loading and updated promotion tests. |
| 2026-08-16 | Step 4 | Deferred Reverse HLS ownership to action-ready and added a full pending-control transcript test. |
| 2026-08-16 | Step 5 | Updated protocol docs and passed host, trace, and release validation. |
