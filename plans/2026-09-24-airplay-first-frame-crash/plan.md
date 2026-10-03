# Plan: AirPlay first-frame crash isolation

> Status: COMPLETED
> Created: 2026-09-24
> Last Updated: 2026-09-24

## Goal
Initialize the H.264 parser context correctly and make the first mirrored frame's processing stage observable without changing protocol behavior.

## Assumptions
- The latest two nxlink disconnects indicate a process exit, but the exact failing instruction is unknown.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] The parser context has H.264 video identity before `av_parser_parse2`.
- [x] Full Trace records first-frame callback, parser, mux header, and packet-write boundaries without per-frame spam.
- [x] Host AirPlay tests and Switch Full Trace build pass.

### Non-goals
- Protocol or lifecycle changes; asserting that the parser is the proven crash cause.

### Edge Cases
- A stream that never yields a valid first frame must remain observable without unbounded logging.

## Design Decisions
None — no design-sensitive changes.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Fix parser context and bracket first-frame stages with bounded diagnostics. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Test | `make test-airplay` | `makefile` | Yes |
| Build | `make full-trace-build BUILD_JOBS=4` | `makefile` | Yes |
| Diff check | `git diff --check` | Git | Yes |

## Context & Learnings
### Key Decisions
- Keep the decoder untouched: the parser only needs a correctly identified codec context.
### Gotchas & Warnings
- Existing uncommitted work predates this task and must be preserved.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/protocol/airplay/media/stream_bridge.c` | Parser and Matroska bridge | Read parser allocation and first-frame path |
| `source/protocol/airplay/mirror/video.c` | Callback boundary | Read access-unit processing |
| `source/protocol/airplay/trace.h` | Existing logging macro | `rg AIRPLAY_TRACE_SYNC` |

### Verified Facts
- `avcodec_alloc_context3(NULL)` is used without setting `codec_id` or `codec_type` before `av_parser_parse2` — verified by source read, 2026-09-24.
- `AIRPLAY_TRACE_SYNC` already provides build-gated diagnostics — verified by source search, 2026-09-24.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-24 | 1 | Set H.264 parser context identity, added first-frame stage logs; host tests and Full Trace build passed. |
