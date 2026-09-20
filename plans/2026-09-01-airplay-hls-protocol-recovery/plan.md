# Plan: AirPlay HLS Protocol Recovery

> Status: COMPLETED
> Created: 2026-09-01
> Last Updated: 2026-09-01

## Goal
Restore repeatable AirPlay HLS playback by making playlist transformation protocol-correct and reducing preparation to the media variants retained in the local master playlist.

## Assumptions
- The latest trace represents one successful YouTube HLS transaction followed by preparation failures and two deterministic large-playlist rewrite failures.
- UxPlay is the applicable local reference for FCUP/reverse-HLS behavior; RPiPlay does not implement this modern HLS relay path.
- No sender-specific YouTube or Bilibili branches will be introduced.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] Condensed playlists larger than 64 KiB and URI components larger than the current fixed scratch arrays are transformed without truncation or false rejection.
- [x] The rewritten master references only media playlists that the receiver will fetch and serve.
- [x] A stopped or replaced HLS transaction leaves no retained media session or player owner and a subsequent `/play` can reach `READY`.
- [x] Existing AirPlay session, remote-video, protocol-coordinator, DLNA, and IPTV behavior remains covered by tests.

### Non-goals
- Sender/application-specific compatibility branches, advertisement skipping, codec policy changes, or mpv/FFmpeg changes.

### Edge Cases
- Empty condensed `PARAMS`, long URLs, mismatched condensed values, duplicate media URIs, alternate audio groups, cancellation before readiness, and replacement after readiness.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Compatibility policy | Sender-specific fixes vs protocol/data-shape behavior | Protocol/data-shape behavior only | yes, requested by user |
| Ownership boundary | Claim player during `/play` vs after HLS preparation | Claim only after a playable local HLS transaction is ready | yes, existing architecture and user direction |
| Buffering | Fixed scratch arrays vs bounded dynamic buffers | Bounded dynamic buffers using existing HLS buffer helper | yes, required by observed valid playlist size |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Make condensed playlist parsing length-safe and diagnostically precise. |
| Step 2 | `steps/step-2.md` | COMPLETED | Select a coherent generic media set and verify repeat-play lifecycle. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| HLS unit tests | Makefile-expanded host compile command plus `build/tests/test_airplay_remote_hls` | `test-airplay` recipe | yes |
| Remote-video tests | Makefile-expanded host compile command plus `build/tests/test_airplay_remote_video` | `test-airplay` recipe | yes |
| Session tests | `make test-airplay-session` | Makefile target | yes |
| Coordinator tests | `make test-protocol-coordinator` | Makefile target | yes |
| Host test suite | `make test-airplay` | Makefile target | yes |
| Switch build | `make dev-build BUILD_JOBS=4` | Makefile target | yes |

## Context & Learnings
### Key Decisions
- HLS transaction state remains in `remote_hls`/`remote_video`; logical sessions identify related control connections, and the coordinator owns only cross-protocol player arbitration.
- Tests protect protocol invariants, but implementation decisions follow the FCUP and HLS flow rather than test-only special cases.

### Gotchas & Warnings
- `AIRPLAY_REMOTE_HLS_MAX_MEDIA_PLAYLISTS` currently permits fewer registered media URIs than a traced master advertised variants; local master output must never reference an unfetched entry.
- Do not log playlist payloads because URLs may contain access tokens.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260901-010204.log` | Failing real-device transcript | `rg` showed no player claim before failures and deterministic `rewrite-failed` on 72,145-byte media data. |
| `source/protocol/airplay/media/remote_hls.c` | FCUP playlist transaction and rewrite implementation | `read` found fixed scratch arrays and eager media request sequencing. |
| `source/protocol/airplay/media/remote_video.c` | Protocol transaction to player ownership boundary | `read` verified claim/load happens only after HLS `READY`. |
| `scripts/test_airplay_remote_hls.c` | Focused host protocol tests | `read` found only a short two-parameter condensed fixture. |
| `scripts/test_airplay_remote_video.c` | Repeat/replacement lifecycle tests | `rg` found direct and HLS replacement coverage. |
| `../others/UxPlay-master/lib/http_handlers.h` | Local reference FCUP flow | `read` verified master then media retrieval followed by `on_video_play`. |
| `../others/UxPlay-master/lib/airplay_video.c` | Local reference playlist storage/expansion | `read` verified dynamic allocation and master media selection support. |

### Verified Facts
- The first HLS transaction balanced `media-retain=1` with `media-release=1` and returned coordinator ownership to none before the second `/play` — verified by `rg` on the latest trace, 2026-09-01.
- Failed later transactions never reached player claim or load, so the immediate regression is before mpv and hardware decoding — verified by ordered trace extraction, 2026-09-01.
- UxPlay and NX-Cast both retrieve media playlists before starting playback; divergence is in selection and parser robustness, not the top-level sequence — verified by source comparison, 2026-09-01.
- Existing HLS buffers and URI resolution helpers can be extended; no new parser module or dependency is required — verified by `rg`/`read` of `remote_hls.c`, 2026-09-01.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-01 | Step 1 | Removed fixed scratch limits from condensed URI expansion and long segment rewriting; added an 80 KiB regression fixture. |
| 2026-09-01 | Step 2 | Rewrote the local master to retain one coherent variant/default audio set, added repeat-HLS coverage, passed the complete AirPlay host suite and Switch build. |
