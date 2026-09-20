# Step 2: Generic AirPlay Media Titles

> Status: COMPLETE
> Created: 2026-09-02

## Goal
Prefer sender-provided generic title fields from `/play` while retaining a safe application-name fallback.

## Prerequisites
- Step 1 completed.
- Files to modify: `source/protocol/airplay/media/remote_video.c` and its focused tests.

## Deliverables
- Bounded title extraction from generic binary plist keys.
- After this step: supplied media titles reach the player UI and missing titles still fall back safely.

## Plan
- [x] `edit` `source/protocol/airplay/media/remote_video.c` — select a bounded title from standard generic key candidates before `clientProcName`.
- [x] `edit` `scripts/test_airplay_remote_video.c` — test title preference, fallback, oversized input, and missing metadata.
- [x] `bash` focused remote-video tests — expect zero failures.

## Quality Checklist
- [x] Evidence-before-edit: target read, callers searched, validation command identified.
- [x] Existing pattern / reuse checked: reuse plist getters and `copy_bounded`.
- [x] Contract understood: metadata remains one owned bounded string passed to existing player command.
- [x] Risk reviewed: untrusted metadata size and malformed plist.
- [x] Mitigation recorded: existing parser limits and destination bounds remain enforced.

## Validation Checklist
- [x] Focused host compilation exits 0.
- [x] Existing fallback behavior remains green.

## Test Checklist
- [x] Generic title wins over `clientProcName`.
- [x] `clientProcName` remains fallback when no title is supplied.
- [x] Invalid or oversized values fail safely.

## Implementation Notes
Binary `/play` metadata checks `title`, `Content-Title`, and `name` before the existing `clientProcName` fallback. Present-but-invalid values fail parsing rather than silently substituting unrelated metadata. `selectedMediaArray` remains untouched because it describes language/media selection.

## Files Changed
- `source/protocol/airplay/media/remote_video.c`
- `scripts/test_airplay_remote_video.c`
