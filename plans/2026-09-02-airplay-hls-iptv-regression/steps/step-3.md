# Step 3: Repair AirPlay Negotiation and Bridge State

> Status: COMPLETED
> Created: 2026-09-02

## Goal
Correct verified AirPlay state transitions so valid media intent reaches the bridge once and replacement media supersedes stale state.

## Prerequisites
- Step 1 completed with exact failing transition evidence.
- Step 2 completed so shared HLS policy is not confounding AirPlay behavior.

## Deliverables
- Protocol-neutral negotiation/bridge lifecycle correction with focused state tests.
- After this step: repeated media sessions and media replacement follow deterministic ownership transitions.

## Plan
- [x] `edit` identified AirPlay route/session source — correct the verified transition or stale-state lifetime.
- [x] `edit` existing AirPlay protocol/state test — cover first play, replacement play, teardown, and reconnect.
- [x] `bash` focused AirPlay host tests — expect all state assertions to pass.

## Quality Checklist
- [ ] Evidence-before-edit: failing trace and exact transition caller read.
- [ ] Existing pattern / reuse checked: use current session identity and media ownership types.
- [ ] Contract understood: control connection lifetime does not implicitly equal media lifetime.
- [ ] Risk reviewed: stale URL reuse, duplicate play, premature stop, and sender reconnect.
- [ ] Mitigation recorded: transition tests based on protocol events, not sender names.

## Validation Checklist
- [ ] Focused AirPlay tests exit 0.
- [ ] `git diff --check` exits 0.

## Test Checklist
- [ ] A second valid play replaces the first URL and generation.
- [ ] Ordinary control disconnect does not forge stop; valid teardown does.

## Implementation Notes
- Latest trace reaches `/play`, completes FCUP playlist acquisition, and dispatches replacement media; the negotiation path is not starved by DLNA or IPTV sockets.
- Reverse HLS exposed both H.264 and VP9 variants and mpv selected VP9. Current bridge changes filter known incompatible video variants when H.264 is available.
- Seeking crossed Googlevideo hosts while FFmpeg attempted connection reuse. The local reverse-HLS policy now adds `http_persistent=no` only for that transport.
- UxPlay tracks playback UUID and handles `playlistRemove`, but its `playlistInsert` remains explicitly unimplemented. Do not invent a new playlist cache lifecycle without a failing protocol trace.
- Existing working-tree fixes separate connection and media lifetime, allow deterministic replacement, filter incompatible master variants when H.264 is available, close local playlist responses, and expose bridge diagnostics. No sender-name branch was added.
- Deviation: UUID playlist caching was not added because the available trace shows explicit `/stop` followed by a fresh `/play`, while even the reference leaves `playlistInsert` unfinished. Adding it now would be speculative state machinery.

## Files Changed
- `source/protocol/airplay/media/remote_hls.c`
- `source/protocol/airplay/media/remote_video.c`
- `source/protocol/airplay/protocol/logical_session.c`
- `scripts/test_airplay_remote_hls.c`
- `scripts/test_airplay_remote_video.c`
- `scripts/test_airplay_session.c`
