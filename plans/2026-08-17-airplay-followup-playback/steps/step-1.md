# Step 1: Fix Condensed HLS Expansion

> Status: COMPLETED
> Created: 2026-08-17

## Goal
Make `hls_expand_condensed()` accept YouTube-style empty `PARAMS=""` while preserving the remainder of each prefixed segment line.

## Prerequisites
- Plan created and approved.
- Files to modify: `source/protocol/airplay/media/remote_hls.c`, `scripts/test_airplay_remote_hls.c`.

## Deliverables
- Empty quoted attributes can be parsed.
- Zero-parameter condensed expansion emits `BASE-URI` plus the original suffix.
- A focused host test proves the rewritten playlist contains absolute segment URLs.

## Plan
- [ ] `read` source/protocol/airplay/media/remote_hls.c — inspect quoted parsing and condensed expansion.
- [ ] `edit` source/protocol/airplay/media/remote_hls.c — allow empty quoted attributes and handle zero `PARAMS`.
- [ ] `edit` scripts/test_airplay_remote_hls.c — add `test_condensed_empty_params` fixture.
- [ ] `bash` make test-airplay — expect exit 0.

## Quality Checklist
- [ ] Evidence-before-edit: target and test read; impact search `YT-EXT-CONDENSED-URL`; validation `make test-airplay`.
- [ ] Existing pattern / reuse checked: bounded `hls_buffer` and existing quoted-attribute helper.
- [ ] Contract understood: empty `PARAMS` is valid; only matching non-empty parameter/value pairs are interleaved.
- [ ] Risk reviewed: URL expansion / bounded buffer / compatibility.
- [ ] Mitigation recorded: focused YouTube-style regression fixture.

## Validation Checklist
- [ ] `git diff --check` exits 0

## Test Checklist
- [ ] `make test-airplay` — all pass

## Implementation Notes
- Quoted HLS attributes now accept empty values.
- Condensed lines with empty `PARAMS` emit `BASE-URI` plus the unchanged suffix.
- Added a host regression that serves the expanded playlist and verifies absolute segment URLs.

## Files Changed
- `source/protocol/airplay/media/remote_hls.c`
- `scripts/test_airplay_remote_hls.c`
