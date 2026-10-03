# Plan: Scoped AirPlay stream teardown

> Status: COMPLETED
> Created: 2026-10-04
> Last Updated: 2026-10-04

## Goal
Correct stream-vs-session teardown and diagnose audio delivery without app-specific exceptions.
## Assumptions
- The latest 200-byte TEARDOWN bodies were not logged; their actual stream selectors remain unknown.
- Hardware continuity and app behavior require another device run.
## Open Questions
None.
## Spec-Lite
### Acceptance Criteria
- [x] Audio-only teardown does not stop active video or close control transport.
- [x] Video-only teardown preserves negotiated audio and transport; empty/full teardown releases everything.
- [x] Late audio with no packets cannot create an empty selected audio track that stalls mpv.
- [x] Logs expose teardown selectors and audio receive/processing/delivery counts without payloads or keys.
### Non-goals
- App filters, network pool changes, detached-session timers or dependency publication.
### Edge Cases
- Malformed/unknown stream selectors, repeat teardown, audio re-setup, missing IDR and teardown before pending replacement.
## Design Decisions
None — protocol scope correction; reuse existing plist parser and runtime replacement worker.
## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | steps/step-1.md | COMPLETED | Implement selective teardown through handler, receiver and runtime |
| Step 2 | steps/step-2.md | COMPLETED | Require audio delivery before activating a new track, add diagnostics and build |
## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Protocol tests | make test-airplay | makefile | yes |
| UI tests | make test-ui | makefile | yes |
| Device build | make full-trace-build BUILD_JOBS=4 NXCAST_REQUIRE_AIRPLAY_MUXER=1 PORTLIBS="<staged revision-4> /opt/devkitpro/portlibs/switch" | prior verified build | yes |
| Whitespace | git diff --check | Git | yes |
## Context & Learnings
### Key Decisions
- Preserve pairing/transport for scoped teardown; full teardown remains terminal.
### Gotchas & Warnings
- Global FFmpeg remains old; use revision-4 staged prefix for device builds.
- Worktree contains earlier uncommitted changes; preserve them.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| logs/run_nxlink-20261003-235050.log | Three stops after TEARDOWN, no decode failures | rg and surrounding events |
| source/protocol/airplay/protocol/handlers.c | Ignores teardown body | handle_teardown read |
| source/protocol/airplay/receiver.c | Marks all successful teardown terminal | receiver_route read |
| source/protocol/airplay/media/mirror_runtime.c | Pending container lifecycle | prior task code |
### Verified Facts
- All three stops in run_nxlink-20261003-235050.log are explicitly issued by our TEARDOWN handler; mpv reports reason=stop/error=success. This is not evidence for a later unsaved run.
- UxPlay lib/raop_handlers.h distinguishes type 96 audio, type 110 video and unscoped session teardown.
- Session 2's new audio track receives zero packets and mpv reports 8492 queued video packets / 157291664 bytes.
- Before this task, the audio worker discarded processing results and had no receive statistics; first-packet and shutdown summaries now expose them.
## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-04 | 1 | Scoped stream stop and selector logging implemented; full host AirPlay suite passes. |
| 2026-10-04 | 2 | First real audio frame plus IDR gate; stream-removal regressions and UI tests pass; revision-4 Full Trace NRO and ELF preserved. New device run still required. |
