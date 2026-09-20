# Step 1: Length-Safe Condensed Playlist Parsing

> Status: COMPLETED
> Created: 2026-09-01

## Goal
Transform valid condensed HLS playlists using bounded dynamic storage without fixed per-line or per-attribute truncation.

## Prerequisites
- Latest failing trace and UxPlay condensed URI implementation have been inspected.
- Files to modify: `source/protocol/airplay/media/remote_hls.c`, `scripts/test_airplay_remote_hls.c`.
- Design confirmed: protocol-shape behavior only, no sender-specific branch.

## Deliverables
- Dynamic parsing of `BASE-URI`, `PARAMS`, `PREFIX`, and condensed URI values with explicit malformed-input rejection.
- Focused tests for large playlists, long values, empty parameters, and mismatched parameter/value counts.
- After this step: the Makefile-expanded HLS host test passes.

## Plan
- [x] `edit` `scripts/test_airplay_remote_hls.c` — add protective fixtures matching the observed large/long condensed playlist boundaries.
- [x] `edit` `source/protocol/airplay/media/remote_hls.c` — replace fixed scratch parsing with existing bounded dynamic buffer operations and preserve strict structural validation.
- [x] `bash` Makefile-expanded HLS host compile/run command — verify all focused HLS parser and serving checks pass.

## Quality Checklist
- [x] Evidence-before-edit: target read `remote_hls.c`, impact search `rg condensed`, validation from the `test-airplay` recipe.
- [x] Existing pattern / reuse checked: reused `AirPlayRemoteHlsBuffer` and existing allocation limits.
- [x] Contract understood: untrusted binary-plist data becomes validated HLS text; malformed input returns a typed action result and no partial state.
- [x] Risk reviewed: memory growth, token leakage, malformed URI expansion, partial transaction state.
- [x] Mitigation recorded: 1 MiB rewritten limit, no payload logging, test malformed counts and long values.

## Validation Checklist
- [x] Makefile-expanded HLS host test exits 0.
- [x] Compiler emits no new warnings for changed targets.

## Test Checklist
- [x] HLS host test — short, large, empty-parameter, and malformed condensed playlists pass expected outcomes.

## Implementation Notes
The protective test failed before implementation with `rewrite-failed`. The parser now allocates quoted attributes dynamically, treats the final condensed value as the remainder of the URI as UxPlay does, and avoids copying non-playlist media lines into the 4095-byte control-URL buffer.

## Files Changed
- `source/protocol/airplay/media/remote_hls.c`
- `scripts/test_airplay_remote_hls.c`
