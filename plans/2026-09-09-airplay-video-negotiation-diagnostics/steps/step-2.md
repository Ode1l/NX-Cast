# Step 2: Full Trace Verification and Device Procedure

> Status: COMPLETED
> Created: 2026-09-09

## Goal
Verify the Switch Full Trace artifact and document how one device run identifies the failed boundary.

## Prerequisites
- Step 1 completed with correlated negotiation diagnostics.

## Deliverables
- Passing Full Trace Switch build.
- Concise Bilibili App-internal AirPlay test procedure and log interpretation guide.

## Plan
- [x] `bash` `make full-trace-build` — verify the diagnostic Switch artifact.
- [x] `edit` `docs/AIRPLAY_DEVELOPMENT.md` — add the focused test sequence and expected stage markers.
- [x] `bash` `git diff --check` — verify patch formatting.

## Quality Checklist
- [x] Evidence-before-edit: Full Trace task and current AirPlay development guide inspected.
- [x] Existing pattern / reuse checked: use existing Full Trace build/task rather than creating another profile.
- [x] Contract understood: device test must use App-internal AirPlay, not Control Center audio-only mode.
- [x] Risk reviewed: ambiguous logs from multiple attempts.
- [x] Mitigation recorded: one clean launch and one casting attempt per captured log.

## Validation Checklist
- [x] `make full-trace-build` exits 0.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] `make test-airplay` remains green after documentation/config verification.

## Implementation Notes
Documented a one-attempt App-internal AirPlay procedure and the meaning of every `first_missing` value. Reused the existing VS Code Full Trace launch profile. Both host tests and the Switch Full Trace build passed.

## Files Changed
- `docs/AIRPLAY_DEVELOPMENT.md`
