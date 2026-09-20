# Step 4: Update Protocol Lifecycle Documentation

> Status: COMPLETED
> Created: 2026-08-17

## Goal
Record that logical connection close retains active playback, while explicit stop, shutdown, natural end, or cross-protocol takeover is the cleanup boundary.

## Prerequisites
- Step 3 completed.
- Files to modify: `docs/AIRPLAY_DEVELOPMENT.md`, `docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md`.

## Deliverables
- Ownership and route contracts describe the corrected AirPlay detached-owner lifecycle.
- Real-device acceptance matrix includes detached AirPlay to DLNA/IPTV takeover.

## Plan
- [ ] `read` docs/AIRPLAY_DEVELOPMENT.md — inspect current lifecycle and hardware acceptance wording.
- [ ] `read` docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md — inspect current ownership and route rules.
- [ ] `edit` docs/AIRPLAY_DEVELOPMENT.md — document retained playback and cross-protocol takeover.
- [ ] `edit` docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md — replace incomplete “new owner claim” wording with explicit stop/shutdown/takeover boundaries.
- [ ] `bash` git diff --check

## Quality Checklist
- [ ] Evidence-before-edit: both docs read; impact search detached/takeover wording; validation diff check.
- [ ] Existing pattern / reuse checked: retain current tables and experimental status.
- [ ] Contract understood: docs match coordinator and integration implementation.
- [ ] Risk reviewed: documentation accuracy / hardware expectations.
- [ ] Mitigation recorded: physical takeover case added to acceptance matrix.

## Validation Checklist
- [ ] `git diff --check` exits 0

## Test Checklist
- [ ] N/A — documentation-only step.

## Implementation Notes
- Updated both AirPlay documents to state that control disconnect retains playback and DLNA/IPTV claims force synchronous takeover.
- Added a dedicated hardware acceptance row for detached AirPlay to DLNA/IPTV takeover.

## Files Changed
- `docs/AIRPLAY_DEVELOPMENT.md`
- `docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md`
