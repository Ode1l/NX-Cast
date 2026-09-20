# Step 2: Compare Reverse-HLS Pipeline

> Status: COMPLETED
> Created: 2026-08-21

## Goal
Locate the exact semantic gap that causes the captured YouTube media playlist to fail before player handoff.

## Prerequisites
- Step 1 completed with direct-play lifecycle conclusions recorded.
- NX-Cast and UxPlay playlist request/store/rewrite paths are available locally.
- No raw signed media URL or private payload will be committed.

## Deliverables
- A field-by-field FCUP request/action comparison.
- A playlist feature matrix covering master, media, relative URI, key/map, condensed tags, and rendition selection.
- After this step: the `rewrite-failed` branch is narrowed to a concrete unsupported or malformed construct.

## Plan
- [x] `read` `source/protocol/airplay/media/remote_hls.c` around action decoding and every rewrite-failed return — map failure branches to required input shapes.
- [x] `read` `../others/UxPlay-master/lib/http_handlers.h`, `lib/fcup_request.h`, and `lib/airplay_video.c` — reconstruct UxPlay request sequence and playlist storage strategy.
- [x] `rg` `scripts/test_airplay_remote_hls.c` and available logs — identify which captured playlist constructs lack regression coverage.
- [x] `bash` a read-only bounded comparison script over source branches and test fixtures — produce a concise feature matrix without persisting private playlist data.

## Quality Checklist
- [x] Evidence-before-edit: parser targets read, impact search completed, validation is branch-to-fixture mapping.
- [x] Existing pattern / reuse checked: current HLS buffer, URL resolver, and playlist tests inventoried before proposing new parsing code.
- [x] Contract understood: `/action` input plist, FCUP correlation fields, playlist output, and next-request side effects.
- [x] Risk reviewed: bounds, signed URL leakage, malformed playlists, and sender compatibility.
- [x] Mitigation recorded: retain bounded parsing and redact URLs in fixtures/logs.

## Validation Checklist
- [x] Every rewrite failure branch has a diagnostic requirement and expected action outcome.
- [x] Differences are labeled verified, inferred, or still unobserved.

## Test Checklist
- [x] N/A — analysis step; future regression fixture is specified but not added here.

## Implementation Notes
UxPlay acknowledges `/action`, stores raw media playlists, and expands YouTube condensed URLs lazily when its local HLS endpoint is requested. NX-Cast eagerly expands and rewrites each media playlist in the `/action` handler and returns HTTP 400 if that work exceeds fixed limits or encounters an unsupported shape. NX-Cast has a 512 KiB raw limit, 1 MiB rewritten limit, 1024-byte `PARAMS` and `PREFIX` buffers, 4095-byte URL buffers, and a 16-playlist table. UxPlay dynamically sizes the condensed expansion and keeps up to ten videos keyed by playback UUID. The existing NX-Cast condensed test contains only two segments and empty parameters, so it does not cover the captured 121310-byte YouTube response. The exact failing subcondition is unobservable because the current log collapses all expansion/rewrite failures into `rewrite-failed`.

## Files Changed
Pending.
