# Plan: AirPlay HLS Size Regression

> Status: COMPLETED
> Created: 2026-09-02
> Last Updated: 2026-09-02

## Goal
Restore YouTube AirPlay HLS playback by separating bounded request and response sizes and covering aggregate condensed-playlist expansion, without changing the unrelated Bilibili control lifecycle.

## Assumptions
- `logs/run_nxlink-20260902-000335.log` represents the reported regression.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] A valid condensed playlist whose rewritten form exceeds 1 MiB can be stored, encoded, and served locally.
- [x] Incoming AirPlay request bodies remain capped at 1 MiB.
- [x] Full AirPlay tests and Switch build pass.
- [x] No Bilibili sender-specific or speculative peer-close behavior is introduced.

### Non-goals
- Keeping a phone control connection open after the sender deliberately closes it.

### Edge Cases
- Aggregate expansion from many short condensed segment lines, allocation failure, and response-size overflow.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Size boundary | Raise all RTSP limits vs separate requests/responses | Keep 1 MiB request cap; allow bounded 4 MiB local playlist responses | yes, minimizes protocol and memory exposure |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add failing aggregate-expansion coverage and implement separate response bounds. |
| Step 2 | `steps/step-2.md` | COMPLETED | Run full host and Switch validation and record Bilibili residual risk. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Focused HLS | Compile and run `test_airplay_remote_hls` | Makefile recipe | yes |
| Full suite | `make test-airplay` | Makefile | yes |
| Switch build | `make dev-build BUILD_JOBS=4` | Makefile | yes |
| Hygiene | `git diff --check` | Git | yes |

## Context & Learnings
### Key Decisions
- Local HLS playlist responses are larger than protocol control requests and need a distinct bounded limit.
### Gotchas & Warnings
- Do not interpret shutdown-time `Connection reset by peer` as playback failure.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260902-000335.log` | Regression trace | Condensed rewrite rejects a 109,751-byte source after aggregate expansion. |
| `source/protocol/airplay/media/remote_hls.c` | Playlist expansion and local serving | Rewritten output now uses the bounded 4 MiB response-body limit. |
| `source/protocol/airplay/protocol/rtsp.[ch]` | Shared request/response representation | Request parsing remains at 1 MiB; response storage and encoding use a distinct 4 MiB limit. |
| `scripts/test_airplay_remote_hls.c` | Regression coverage | Existing 80 KiB fixture expands only one URI and stays below 1 MiB. |

### Verified Facts
- YouTube receives HTTP 400 `rewrite-failed` before player ownership or mpv load — verified from latest trace, 2026-09-02.
- Both Bilibili URLs load, hardware-decode, present a first frame, and continue until manual Home; only sender control/reverse TCP closes — verified from latest trace, 2026-09-02.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-02 | Step 1 | Added a 4,096-segment condensed fixture, reproduced the 1 MiB aggregate-expansion failure, and separated the 1 MiB request limit from a 4 MiB response limit. |
| 2026-09-02 | Step 2 | Extended coverage through full response encoding; full AirPlay tests, normal Switch build, and diff hygiene checks passed. |
