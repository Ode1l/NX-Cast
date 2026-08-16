# Plan: Complete AirPlay Video Compatibility

> Status: COMPLETED
> Created: 2026-08-16
> Last Updated: 2026-08-16

## Goal
Complete the video-oriented AirPlay compatibility path so reverse-channel HLS, mirroring, and associated or audio-first media reach the existing NX-Cast player safely without regressing DLNA or IPTV.

## Assumptions
- The existing protocol coordinator and player actor remain the only owners of playback mutations.
- UxPlay behavior at commit `a3c19cbc7fcc870d74a0960bc97817a2569b4808` is the interoperability reference, while implementation remains native NX-Cast C code.
- Real iPhone/Switch interoperability requires hardware validation after host and strict Switch builds pass.
- The current dirty worktree contains user-owned and prior AirPlay work that must be preserved.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] A reverse AirPlay connection can be upgraded, registered by logical session, and used for serialized outbound HTTP events.
- [x] Non-absolute AirPlay HLS play requests can obtain bounded playlists through FCUP, expose rewritten local playlists, and load the resulting URL through the player actor.
- [x] H.264 type-110 mirroring remains functional and audio arriving before or without video has a defined, bounded playback path.
- [x] Session teardown, reconnect, server shutdown, and competing protocol ownership do not leak clients, race sends, or mutate mpv from network threads.
- [x] Host tests, strict Switch trace build, release build, and documentation checks pass, except real-device behavior explicitly left for hardware validation.

### Non-goals
- AirPlay 2 multi-room audio, AWDL, HEVC mirroring, commercial DRM/MFi, GStreamer, and music-player features.
- Replacing the existing protocol coordinator, player actor, FFmpeg, mpv, or deko3d backend.
- General-purpose HLS proxying for DLNA or IPTV.

### Edge Cases
- Reverse connection disappears during an event send; playlist or action bodies exceed limits; audio SETUP precedes type-110 SETUP; teardown races with a worker send; an old logical session reconnects after a new owner is active.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Control concurrency | New global state machine vs existing coordinator plus connection-local synchronization | Reuse coordinator/player actor; add only a per-client send lock and session-scoped remote-media state | yes |
| HLS delivery | GStreamer/proxy service vs bounded in-process playlist compatibility layer | In-process FCUP and local playlist routes on the AirPlay HTTP server | yes |
| Audio compatibility | Separate player backend vs generalize current stream bridge | Generalize current bridge/runtime with explicit media profile and safe late-video promotion | yes |
| Scope | Full AirPlay 2 vs video receiver subset | Mirroring, associated audio, audio-first compatibility, and URL/HLS playback only | yes |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add a lifecycle-safe PTTH reverse transport primitive and `/reverse` upgrade path. |
| Step 2 | `steps/step-2.md` | COMPLETED | Implement bounded FCUP/HLS acquisition and local playlist delivery through `/play` and `/action`. |
| Step 3 | `steps/step-3.md` | COMPLETED | Support audio-first and audio-only-arrival playback without bypassing the existing player actor. |
| Step 4 | `steps/step-4.md` | COMPLETED | Harden the protocol matrix, logical-session teardown, reconnect, and concurrent ownership behavior. |
| Step 5 | `steps/step-5.md` | COMPLETED | Run full host/Switch verification and document the implemented support and hardware test matrix. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Formatting | `git diff --check` | Git worktree convention | yes |
| Host AirPlay tests | `make test-airplay` | `makefile` | yes |
| Strict trace build | `make full-trace-build BUILD_JOBS=4` | `makefile` | yes |
| Release build | `make release-build RELEASE_JOBS=4` | `makefile` | yes |

## Context & Learnings
### Key Decisions
- Keep media and protocol state session-scoped; network workers may parse and enqueue but may not call mpv directly.
- Use bounded synchronous reverse writes under a connection-local lock; do not add another thread until measurements require one.
- Serve only active, tokenized AirPlay playlists and resolve relative playlist URIs before exposing them to mpv.
### Gotchas & Warnings
- Matroska stream headers cannot gain a new video stream after writing begins; late type-110 promotion must rotate the bridge generation rather than mutate an emitted header.
- The current working tree is intentionally dirty; unrelated changes must not be reverted or reformatted.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/protocol/airplay/server.c` | Owns client workers and socket lifecycle | Read during protocol audit; static bounded client table observed. |
| `source/protocol/airplay/protocol/rtsp.c` | Parses requests and encodes normal responses | Read during protocol audit; no outbound request encoder observed. |
| `source/protocol/airplay/handlers.c` | Dispatches RTSP/HTTP AirPlay routes | Read and compared with UxPlay route matrix. |
| `source/protocol/airplay/media/remote_video.c` | Owns remote URL playback state | Read during protocol audit; direct URLs only. |
| `source/protocol/airplay/media/stream_bridge.c` | Muxes mirror video/audio into mpv input | Read during protocol audit; currently always creates H.264 stream. |
| `source/protocol/airplay/mirror_runtime.c` | Serializes mirror player mutations | Read during protocol audit; existing bounded command queue verified. |
| `/tmp/nxcast-uxplay-audit-20260816/lib/raop.c` | External behavior reference | Checked at commit `a3c19cbc7fcc870d74a0960bc97817a2569b4808`. |
### Verified Facts
- `make test-airplay` passes before these edits — verified by local command on 2026-08-16.
- Existing mirror playback mutations are serialized through `mirror_runtime` rather than issued directly by handler workers — verified by source read on 2026-08-16.
- UxPlay uses `/reverse`, reverse `POST /event`, `/action`, and local master/media playlist routes for non-absolute HLS content locations — verified from the reference checkout on 2026-08-16.
- A PTTH connection can now survive client HTTP responses and send repeated serialized events; socket and worker diagnostics return to zero after disconnect and stop — verified by `test-airplay-server-lifecycle` on 2026-08-16.
- Remote HLS FCUP responses are matched by logical session, request ID, and URL; local token routes become unavailable after reset — verified by `test_airplay_remote_hls` on 2026-08-16.
- Audio-only generations emit playable Matroska without a fake H.264 stream, and a late type-110 stream performs an actor-serialized generation replacement — verified by `test_airplay_audio` and `test_airplay_mirror_runtime` on 2026-08-16.
- Reverse HTTP connections now bind to the exact bounded logical Apple session, replace stale sockets, and serialize concurrent event sends without body interleaving — verified by session and server lifecycle stress tests on 2026-08-16.
- The strict trace and release Switch builds link successfully with the pinned Matroska-enabled FFmpeg archive; the SD package contains IPTV presets and excludes AirPlay identity/pairing material — verified on 2026-08-16.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-16 | Step 1 | Added bounded outbound request encoding, PTTH upgrade registration, serialized reverse writes, dedicated reverse response consumption, and lifecycle tests. |
| 2026-08-16 | Step 2 | Added bounded reverse HLS acquisition, relative URI rewriting, loopback-only token routes, delayed actor load, and protocol transcript tests. |
| 2026-08-16 | Step 3 | Added immutable audio-only/A-V bridge profiles, audio master-clock mode, media RECORD activation, and safe late-video generation replacement. |
| 2026-08-16 | Step 4 | Bound reverse and URL routes to logical sessions, made reverse replacement deterministic, added bounded reconnect/send stress tests, and documented stable route status behavior. |
| 2026-08-16 | Step 5 | Passed host, trace, release, and package validation; updated the experimental support contract and left real iPhone/Switch cases explicitly pending. |
