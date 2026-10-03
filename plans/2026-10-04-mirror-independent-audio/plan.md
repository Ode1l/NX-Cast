# Plan: Independent mirror audio track

> Status: COMPLETED
> Created: 2026-10-04
> Last Updated: 2026-10-04

## Goal
Deliver mirrored audio without waiting for another video IDR or restarting video.

## Assumptions
- Device sound and lip sync need a follow-up device run; host tests cannot verify hos output.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] First real mirrored audio activates audio without a new IDR or video reload (host runtime).
- [x] Audio teardown/re-SETUP leaves video and ownership intact (host runtime).
- [x] Independent audio timestamps retain the video origin; standalone audio remains a stub (clock/runtime tests).
- [x] Stale async audio opens cannot attach to a replacement audio generation (unique URI and reply validation; native device acceptance pending).
### Non-goals
- New mpv instances, app filters, network changes, standalone music or publishing.
### Edge Cases
- No audio, audio before video, missing sync, teardown during asynchronous open, full stop.

## Design Decisions
| Decision | Rationale |
|---|---|
| Separate mirror audio container, same mpv instance | No IDR dependency, no restart of video or second player |
| Native async audio-add through existing actor | Existing ownership validation and retained payload, no new project worker |
| Shared origin with mirror-only timestamp rebasing disabled | Late audio must not be re-timestamped to zero |
| Audio snapshots video timeline, never reverse | Audio backpressure must not block the video mux thread |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | steps/step-1.md | COMPLETED | Independent runtime audio container and shared timeline regression |
| Step 2 | steps/step-2.md | COMPLETED | Connect audio through actor/libmpv and build Full Trace |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Protocol | make test-airplay | makefile | yes |
| UI | make test-ui | makefile | yes |
| Device | make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1 PORTLIBS="<staged revision-4> /opt/devkitpro/portlibs/switch" | prior verified build | yes |
| Whitespace | git diff --check | Git | yes |

## Context & Learnings
### Key Decisions
- Video container stays video-only; independent audio container uses original timeline, not its own zero.
- Reuse runtime worker and ownership-validated media actor, never issue player commands from RTP callbacks.
### Gotchas & Warnings
- Global FFmpeg is still revision 3; use staged revision 4 for this build.
- Audio-only playback remains deliberately unsupported; this task is mirroring audio.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| logs/run_nxlink-20261004-005154.log | Audio reaches runtime but waits forever for IDR | first-frame-ready and final delivered counts |
| source/protocol/airplay/media/mirror_runtime.c | Artificial keyframe gate | runtime_video/runtime_audio |
| source/player/backend/libmpv.c | Existing custom stream and async API | stream callback and event handling |
| source/player/core/session.c | Existing retained bridge actor command | player_submit_airplay_stream_bridge |
### Verified Facts
- Latest audio delivered counts are 1247 and 2091; bridge audio_packets stays zero.
- Actual scoped TEARDOWN selector is 96, and video now remains active.
- First audio starts after video IDR; second audio also starts after last logged IDR.
- mpv 0.36 mp_add_external_file rebases timestamps when rebase-start-time=yes; mirror playback must use no.
- mpv provides asynchronous audio-add and external track removal; UxPlay routes audio and video separately.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-04 | 1 | No-IDR late audio activation/re-SETUP and four-second shared-origin regression passed |
| 2026-10-04 | 2 | Native asynchronous external audio, bounded diagnostics, Full Trace build and matching ELF archived; device sound/sync verification pending |
