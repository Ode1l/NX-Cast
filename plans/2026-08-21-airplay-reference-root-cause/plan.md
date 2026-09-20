# Plan: AirPlay Reference Root-Cause Study

> Status: COMPLETED
> Created: 2026-08-21
> Last Updated: 2026-08-21

## Goal
Establish evidence-backed root causes for the current YouTube reverse-HLS and Bilibili direct-play failures by comparing NX-Cast with UxPlay and RPiPlay before changing protocol code.

## Assumptions
- The newest available trace is `logs/run_nxlink-20260817-210916.log`; no newer runtime log is present in the workspace.
- UxPlay 1.73 is the primary reference for modern AirPlay URL/HLS behavior, while RPiPlay is a secondary reference for RAOP and mirror transport behavior.
- This task is analysis-only; implementation follows in a separate execution plan after the failure contracts are established.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [ ] The YouTube failure is located before or after player handoff with an exact NX-Cast/reference behavior difference.
- [ ] The Bilibili stop is traced from socket/session event to player command and compared with UxPlay lifecycle behavior.
- [ ] Any remaining uncertainty is converted into one minimal device test with explicit required log fields.

### Non-goals
- Modifying AirPlay protocol, player, FFmpeg, or build code in this task.
- Implementing complete AirPlay 2, DRM, MFi, HEVC, or audio-only playback.

### Edge Cases
- iOS may intentionally close a short-lived HTTP control connection after handing off a direct URL; connection lifetime must not be assumed to equal media lifetime.
- A sender may fall back from AirPlay to DLNA, producing a new request for the same content with a different signed URL.

## Design Decisions

| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Reference hierarchy | UxPlay only; RPiPlay only; UxPlay primary plus RPiPlay transport cross-check | UxPlay primary plus RPiPlay transport cross-check | yes |
| Investigation method | Broad speculative logging; transcript-first route comparison | Transcript-first route comparison, then one targeted device trace only if needed | yes |
| Runtime reuse | Import Linux/GStreamer runtime; reimplement protocol semantics over NX-Cast interfaces | Reimplement only verified protocol semantics over existing NX-Cast C interfaces | yes |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Compare direct-play connection and media lifecycle behavior. |
| Step 2 | `steps/step-2.md` | COMPLETED | Compare reverse-HLS playlist request, storage, and rewrite behavior. |
| Step 3 | `steps/step-3.md` | COMPLETED | Consolidate root causes and define the minimum next validation or fix plan. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Trace chronology | `rg -n "uri=/play|uri=/action|first-frame|logical-close|detached-cleanup|rewrite-failed" logs/run_nxlink-20260817-210916.log` | Existing trace format | yes |
| Existing AirPlay tests | `make test-airplay` | `makefile` | no, analysis-only |
| Plan integrity | `git diff --check -- plans/2026-08-21-airplay-reference-root-cause` | Git | yes |

## Context & Learnings
### Key Decisions
- Media lifetime and control-connection lifetime will be reviewed as separate state dimensions.
- A passing host parser test is not sufficient evidence of compatibility with the captured YouTube playlist.
### Gotchas & Warnings
- The working tree already contains user changes across AirPlay files; this analysis must not attribute all current code to the logged binary without checking diffs.
- `recv: Connection reset by peer` appears at application shutdown and is not treated as the playback root cause.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260817-210916.log` | Latest device chronology | `find`, `stat`, `rg`, and numbered line reads on 2026-08-21 |
| `source/protocol/airplay/media/remote_video.c` | NX-Cast remote-video lifecycle | `rg` and numbered read on 2026-08-21 |
| `source/protocol/airplay/media/remote_hls.c` | NX-Cast reverse-HLS parsing and rewrite | `rg` and diff inspection on 2026-08-21 |
| `source/protocol/airplay/integration.c` | Player ownership and detached cleanup | `rg` and numbered read on 2026-08-21 |
| `../others/UxPlay-master/lib/http_handlers.h` | UxPlay URL/HLS HTTP behavior | `rg` and numbered read on 2026-08-21 |
| `../others/UxPlay-master/lib/raop.c` | UxPlay connection classification/destruction | `rg` and numbered read on 2026-08-21 |
| `../others/UxPlay-master/lib/airplay_video.c` | UxPlay playlist state/store | targeted comparison scheduled in Step 2 |
| `../others/RPiPlay-master/lib/raop.c` | RAOP/mirror close behavior cross-check | `rg` on 2026-08-21 |

### Verified Facts
- YouTube reverse-HLS is rejected at the second `/action` with `reason=rewrite-failed` before any player load — verified by `rg`/numbered trace read, 2026-08-21.
- Bilibili direct play reaches mpv file-loaded, nvtegra decode, audio output, and first-frame presentation — verified by numbered trace read, 2026-08-21.
- NX-Cast schedules detached cleanup after the final logical AirPlay connection closes and later releases the AirPlay owner through a path that stops playback — verified by trace and source reads, 2026-08-21.
- UxPlay's generic connection-destroy callback updates connection/audio state but does not invoke its HLS video-stop callback; explicit `POST /stop` has a separate handler — verified by `../others/UxPlay-master/lib/raop.c`, `lib/http_handlers.h`, and `uxplay.cpp`, 2026-08-21.
- The later old Bilibili media request is a new DLNA `SetAVTransportURI` request with a changed signed query, not an internal NX-Cast replay — verified by trace comparison, 2026-08-21.
- NX-Cast eagerly expands valid media playlists while handling `/action`, whereas UxPlay stores raw media playlists and expands YouTube condensed URLs only when the local HLS endpoint is requested — verified by source comparison, 2026-08-21.
- NX-Cast's current YouTube fixture covers only two segments with empty `PARAMS`; it does not represent the captured 121310-byte response — verified by `scripts/test_airplay_remote_hls.c`, 2026-08-21.
- UxPlay keeps multiple AirPlay video records keyed by playback UUID for resume/reconnect and treats expected `/getProperty` calls as benign unhandled compatibility requests; NX-Cast currently lacks the cache and returns 501 — verified by source and trace comparison, 2026-08-21.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-21 | Step 1 | Started lifecycle comparison using the latest device trace and local UxPlay/RPiPlay sources. |
| 2026-08-21 | Step 1 | Confirmed NX-Cast incorrectly stops direct URL media after generic AirPlay control detachment; UxPlay separates connection destruction from HLS media stop. |
| 2026-08-21 | Step 2 | Confirmed NX-Cast eagerly rewrites YouTube media playlists during `/action`, unlike UxPlay's raw-store/lazy-expand pipeline; exact failed bound needs substage telemetry. |
| 2026-08-21 | Step 3 | Consolidated a four-slice implementation boundary and determined that retesting the current binary would add no useful evidence. |
