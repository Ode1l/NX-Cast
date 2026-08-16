# Step 2: Remote HLS Compatibility

> Status: COMPLETED
> Created: 2026-08-16

## Goal
Turn non-absolute AirPlay HLS play requests into a bounded local playlist URL that the existing player actor can load.

## Prerequisites
- Step 1 completed — reverse events can be sent by logical session.
- Files to modify: `media/remote_video.*`, new focused HLS module if justified, `handlers.*`, receiver/server integration, and host tests.
- Direct absolute HTTP/HTTPS URLs remain pass-through and existing DLNA/IPTV routing remains untouched.

## Deliverables
- `/play` classifies direct URLs versus reverse HLS requests; `/action` consumes matching FCUP responses.
- Active HLS sessions validate body sizes, resolve and rewrite playlist URIs, and serve tokenized local master/media playlists.
- Player load occurs only after a usable local playlist is ready and still belongs to the active logical session.

## Plan
- [x] `write` bounded remote HLS session component — model FCUP request/response correlation, playlist limits, URI resolution, and local routes.
- [x] `edit` `remote_video.*` and `handlers.*` — wire `/play`, `/action`, stop, teardown, and local GET handling.
- [x] `edit` receiver/server integration — inject reverse-send transport and local listener URL without global socket access.
- [x] `write` host transcript tests — direct URL, master/media FCUP, malformed bodies, stale responses, stop, and reconnect.
- [x] `bash` `make test-airplay` — expect 0 failures.

## Quality Checklist
- [ ] Evidence-before-edit: target read `remote_video.c`, receiver dispatch, UxPlay HLS routes; impact search `rg "remote_video|/play|/action"`
- [ ] Existing pattern / reuse checked: player load operations and logical-session ownership already in `integration.c`
- [ ] Contract understood: one active remote HLS state per remote-video owner; no unbounded playlist allocations or background worker
- [ ] Risk reviewed: untrusted parsing, SSRF surface, stale session writes, resource lifecycle
- [ ] Mitigation recorded: strict schemes, bounded counts/sizes, tokenized routes, session/generation matching, negative tests

## Validation Checklist
- [x] `git diff --check` exits 0
- [x] HLS host targets compile with warnings as errors

## Test Checklist
- [x] Remote HLS parser and transcript tests pass
- [x] `make test-airplay` passes

## Implementation Notes
- A session-scoped HLS component owns only bounded parsing and playlist state; it owns no socket or player thread.
- Reverse events use the Step 1 serialized transport. Player load starts only after all referenced playlists are ready and the generation remains active.
- Local routes are tokenized and accepted only from the loopback peer used by mpv.

## Files Changed
- `source/protocol/airplay/media/remote_hls.c`
- `source/protocol/airplay/media/remote_hls.h`
- `source/protocol/airplay/media/remote_video.c`
- `source/protocol/airplay/media/remote_video.h`
- `source/protocol/airplay/receiver.c`
- `source/protocol/airplay/integration.c`
- `scripts/test_airplay_remote_hls.c`
- `makefile`
