# Step 1: First-frame parser initialization and stage diagnostics

> Status: COMPLETED
> Created: 2026-09-24

## Goal
Make the parser context valid and expose where the first mirrored frame stops.

## Prerequisites
- Target files and callers read; user authorized the implementation.

## Deliverables
- H.264 video context fields initialized before parser use.
- One-time first-frame callback, parser, header, and write-boundary logs.
- Host tests and Switch Full Trace build pass.

## Plan
- [x] `edit` `source/protocol/airplay/media/stream_bridge.c` — initialize parser context and add bounded stage logs.
- [x] `edit` `source/protocol/airplay/mirror/video.c` — log first keyframe callback entry and return.
- [x] `bash` `make test-airplay` — zero failures.
- [x] `bash` `make full-trace-build BUILD_JOBS=4` — NRO built.
- [x] `bash` `git diff --check` — no whitespace errors.

## Quality Checklist
- [x] Evidence-before-edit: read both targets, searched existing trace patterns, identified `make` validation.
- [x] Existing pattern / reuse checked: `AIRPLAY_TRACE_SYNC` in `trace.h`.
- [x] Contract understood: no transport or callback behavior changes; diagnostics are build-gated.
- [x] Risk reviewed: logs may increase latency only on first frame.
- [x] Mitigation recorded: gate logs on first frame and run host tests plus target build.

## Validation Checklist
- [x] `make full-trace-build BUILD_JOBS=4` exits 0.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] `make test-airplay` passes.

## Implementation Notes
Existing synthetic first-frame bridge test reached parser-end, header-end, and packet-write-end. Real-device outcome remains unverified.

## Files Changed
- `source/protocol/airplay/media/stream_bridge.c`
- `source/protocol/airplay/mirror/video.c`
