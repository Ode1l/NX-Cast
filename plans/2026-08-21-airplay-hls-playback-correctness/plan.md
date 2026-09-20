# Plan: AirPlay Reverse HLS Playback Correctness

> Status: COMPLETED
> Created: 2026-08-21
> Last Updated: 2026-08-21

## Goal
Make AirPlay Reverse HLS use FFmpeg's HLS demuxer, preserve audio/video rendition handling, and expose enough bounded diagnostics to verify YouTube playback without regressing direct AirPlay, DLNA, or IPTV.

## Assumptions
- Bilibili control peer closure without `/stop` remains a detached control state, not an implicit media stop.
- YouTube ads are not filtered; the receiver must play the sender-provided HLS timeline correctly.
- The installed Switch FFmpeg build includes the HLS demuxer already used by IPTV and DLNA HLS playback.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] Local `/airplay-hls/` URLs are loaded with `demuxer-lavf-format=hls`, while other media URLs retain their current options.
- [x] A master playlist with independent audio and video renditions remains addressable through local rewritten URLs.
- [x] Reverse HLS logs bounded playlist topology and serving decisions without logging signed media URLs.
- [x] Known AirPlay property probes return a benign compatibility response instead of `501`.
- [x] Host AirPlay tests and the Switch full-trace build pass.

### Non-goals
- Blocking or skipping YouTube advertisements.
- Inferring a Stop from Bilibili's ordinary control connection closure.
- Changing DLNA or IPTV playback policy.

### Edge Cases
- Direct media playlists without a master must continue to load.
- Unknown property probes remain unsupported.
- Signed playlist URLs and tokens must not be written to normal diagnostics.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Reverse HLS demux selection | Rely on MIME/autodetect; force FFmpeg HLS per local AirPlay URL; change global demux behavior | Force FFmpeg HLS only for `/airplay-hls/` URLs because logs prove mpv's generic M3U path is iterating fragments | yes, derived from the user's request to fix the observed playback behavior without app-specific hacks |
| Detached Bilibili exit | Treat peer-close as Stop; keep detached playback; add arbitrary timeout | Keep detached playback and require explicit Stop/EOS because peer-close previously caused one-second disconnects | yes, preserves the behavior the user confirmed now plays |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Force the correct HLS demux path with focused option-policy tests. |
| Step 2 | `steps/step-2.md` | COMPLETED | Add bounded Reverse HLS diagnostics and compatibility responses, then run full validation. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Focused player policy test | `make test-player-cache-policy` | `makefile` target discovered with `rg` | yes |
| AirPlay regression suite | `make test-airplay` | `makefile` target discovered with `rg` | yes |
| Switch build | `make full-trace-build -j4` | Existing tested developer workflow | yes |
| Whitespace safety | `git diff --check` | Git built-in | yes |

## Context & Learnings
### Key Decisions
- Keep protocol lifecycle and media lifecycle separate; only explicit terminal protocol events or player terminal state stop detached playback.
- Apply demux forcing as a per-file option so the fix cannot change DLNA, IPTV, or direct URL behavior.
- Known sender log probes are acknowledged through an exact allowlist; unknown `getProperty` names remain unsupported.

### Gotchas & Warnings
- The latest log contains signed Googlevideo URLs; diagnostics must report counts, indexes, and hashes rather than full playlist URLs.
- The working tree already contains ongoing AirPlay changes and must not be reset or rewritten wholesale.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260821-193121.log` | Runtime evidence | `rg`/`sed` showed repeated EOF/start on one local master, video-only tracks, explicit YouTube `/stop`, and Bilibili peer-close. |
| `source/player/backend/libmpv.c` | Per-file load options | `nl` showed all loads use cache policy options in `libmpv_async_load_current`. |
| `source/player/cache_policy.c` | Existing option formatter | `rg` showed the reusable per-file option construction boundary. |
| `source/protocol/airplay/media/remote_hls.c` | Reverse HLS rewrite and serve boundary | `nl` verified audio URI attributes are registered but topology and served indexes are not logged. |
| `source/protocol/airplay/media/remote_video.c` | AirPlay HTTP route boundary | `rg` verified supported `/action`, `/playback-info`, `/stop`, rate, and scrub routes. |
| `source/protocol/airplay/protocol/handlers.c` | AirPlay HTTP compatibility routes | Exact property-probe allowlist belongs at the protocol dispatch boundary. |
| `scripts/test_player_cache_policy.c` | Focused per-file policy tests | `rg` verified an existing host test target. |
| `scripts/test_airplay_remote_hls.c` | HLS master/media regression tests | `nl` verified an existing independent audio/video rendition fixture. |
| `../others/UxPlay-master/lib/http_handlers.h` | Reference behavior | `sed` verified explicit playlist action handling and master/media URI table construction. |

### Verified Facts
- YouTube Reverse HLS repeatedly reaches EOF and starts the same local master while advancing individual `gosq` fragment URLs, which is consistent with generic M3U playlist traversal rather than one HLS demux session — verified by `rg` and `sed` on the latest log, 2026-08-21.
- The YouTube sessions expose only `Video --vid=1`; no `Audio --aid` or `AO: [hos]` appears after their local HLS loads — verified by log search, 2026-08-21.
- YouTube sends explicit `POST /stop`; Bilibili's AirPlay direct path closes its peer without `/stop` — verified by log search, 2026-08-21.
- `hls_rewrite_master_locked` already rewrites playlist-valued URI attributes such as `EXT-X-MEDIA` and bare variant lines — verified by source read, 2026-08-21.
- All libmpv loads have an existing per-file options string, so no new abstraction or global mpv option is required — verified by source read, 2026-08-21.
- The generated AirPlay master retains the `EXT-X-MEDIA` audio URI and the associated `AUDIO` group on its video variant — verified by the strengthened host fixture and `make test-airplay`, 2026-08-21.
- Full-trace Switch compilation accepts the extended policy field and diagnostics — verified by `make full-trace-build -j4`, 2026-08-21.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-21 | Step 1 | Added a bounded per-file policy flag that forces FFmpeg HLS demuxing only for local Reverse HLS URLs; focused and full AirPlay tests passed. |
| 2026-08-21 | Step 2 | Added bounded playlist topology/serve diagnostics, exact compatibility acknowledgements, and demux selection tracing; host tests and Switch full-trace build passed. |
