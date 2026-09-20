# Step 2: Harden HLS Diagnostics And Compatibility

> Status: COMPLETED
> Created: 2026-08-21

## Goal
Expose bounded Reverse HLS topology/serve evidence and acknowledge known sender property probes without changing media lifecycle semantics.

## Prerequisites
- Step 1 completed with local AirPlay HLS using FFmpeg's HLS demuxer.
- Files to modify: `source/protocol/airplay/media/remote_hls.c`, `source/protocol/airplay/media/remote_video.c`, `scripts/test_airplay_remote_hls.c`, `scripts/test_airplay_remote_video.c` or `scripts/test_airplay_handlers.c`.
- Design: diagnostics contain counts/indexes/hashes, never signed source URLs.

## Deliverables
- Master/media collection and local playlist serving are observable with bounded logs.
- Known `getProperty` compatibility probes receive a benign `200` response while unknown routes remain `501`.
- After this step: host tests, full-trace Switch build, and diff checks pass.

## Plan
- [x] `edit` `scripts/test_airplay_remote_hls.c` — assert independent audio/video master topology remains locally served.
- [x] `edit` `source/protocol/airplay/media/remote_hls.c` — log action type, playlist topology, collected media count, and served local index without source URLs.
- [x] `edit` AirPlay route tests and `source/protocol/airplay/protocol/handlers.c` — return `200` for known playback property probes and preserve `501` for unknown properties.
- [x] `edit` `source/player/backend/libmpv.c` — expose `demux=hls|auto` in existing full-trace media diagnostics.
- [x] `bash` `make test-airplay` — zero failures.
- [x] `bash` `make full-trace-build -j4` — successful Switch build.
- [x] `bash` `git diff --check` — no whitespace errors.

## Quality Checklist
- [x] Evidence-before-edit: target read `remote_hls.c`/`handlers.c`, impact search `rg "getProperty|remote_hls"`, validation `make test-airplay`
- [x] Existing pattern / reuse checked: use `AIRPLAY_TRACE`, existing empty compatibility responses, and existing route dispatch
- [x] Contract understood: diagnostics are side effects only; known compatibility probes are read-only acknowledgements
- [x] Risk reviewed: sensitive URL leakage, noisy logs, route overmatching
- [x] Mitigation recorded: no URL payload logging, one summary per playlist/serve, exact property allowlist

## Validation Checklist
- [x] `make test-airplay` exits 0
- [x] `make full-trace-build -j4` exits 0
- [x] `git diff --check` exits 0

## Test Checklist
- [x] Independent audio/video rendition fixture passes.
- [x] Known property probes return `200`; unknown properties and routes remain `501`.

## Implementation Notes
Playlist diagnostics are emitted once per collected playlist and first local serve, with no source URL. The observed sender probes are handled in the protocol handler because they are HTTP compatibility routes rather than media commands. A small direct dependency added `demux=hls|auto` to the existing libmpv trace line so real-device verification is unambiguous.

## Files Changed
- `source/protocol/airplay/media/remote_hls.c`
- `source/protocol/airplay/protocol/handlers.c`
- `source/player/backend/libmpv.c`
- `scripts/test_airplay_remote_hls.c`
- `scripts/test_airplay_handlers.c`
