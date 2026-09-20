# Step 1: Restore Complete HLS Relay

> Status: COMPLETED
> Created: 2026-09-01

## Goal
Preserve every valid sender-advertised HLS media reference while retaining length-safe playlist rewriting.

## Prerequisites
- Latest trace and UxPlay HLS relay flow have been inspected.
- Files to modify: `source/protocol/airplay/media/remote_hls.c`, `scripts/test_airplay_remote_hls.c`.
- Design: mpv, not the AirPlay protocol layer, selects the playable variant.

## Deliverables
- Master rewriting registers and locally rewrites all playlist URI attributes and variant URI lines.
- Regression coverage verifies multiple video variants and alternate audio remain available.
- After this step: focused and aggregate AirPlay host tests pass.

## Plan
- [x] `edit` `scripts/test_airplay_remote_hls.c` — replace single-highest-variant expectation with complete-master/media request coverage.
- [x] `edit` `source/protocol/airplay/media/remote_hls.c` — remove bandwidth selection and restore generic all-reference rewriting using existing bounded helpers.
- [x] `bash` focused AirPlay HLS/remote-video/crypto tests — verify HLS, direct playback, repeat lifecycle, and the transient crypto-test result.

## Quality Checklist
- [x] Evidence-before-edit: target read `remote_hls.c`, impact search `rg hls_rewrite_master_locked`, validation from the `test-airplay` recipe.
- [x] Existing pattern / reuse checked: reused registry, URI rewriter, and UxPlay's full-master flow.
- [x] Contract understood: sender master in, equivalent local master out; each local URI has stored media data before READY.
- [x] Risk reviewed: preparation latency, registry capacity, malformed URI, duplicate media entries.
- [x] Mitigation recorded: bounded registry, deduplication, existing validation, multi-variant transcript test.

## Validation Checklist
- [x] Focused HLS, remote-video, and crypto tests exit 0.
- [x] `git diff --check` exits 0 for this step.

## Test Checklist
- [x] Multi-variant/audio master requests and serves every retained media playlist.
- [x] Large condensed and malformed playlist tests remain green.

## Implementation Notes
The protective four-media test failed under the previous highest-bandwidth implementation, then passed after restoring complete master rewriting. The aggregate run reached an unrelated probabilistic crypto test failure because its corruption byte can equal the existing random byte; the same generated crypto binary passed immediately standalone. Aggregate validation will be rerun after Step 2.

## Files Changed
- `source/protocol/airplay/media/remote_hls.c`
- `scripts/test_airplay_remote_hls.c`
