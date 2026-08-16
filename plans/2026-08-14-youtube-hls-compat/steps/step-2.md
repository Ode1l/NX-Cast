# Step 2: Validate the Compatibility Patch

> Status: COMPLETED
> Created: 2026-08-14

## Goal
Verify the complete host suite and Switch build, then define the next real-device evidence required to distinguish negotiation from media-bridge failures.

## Prerequisites
- Step 1 completed with all focused handler tests passing.
- Local devkitPro and selected AirPlay dependencies are available for the Switch build.
- Files to modify: plan records only unless validation exposes a scoped defect.

## Deliverables
- Regression and diff-hygiene evidence.
- A strict trace NRO when local dependencies permit.
- After this step: the next device test has an explicit expectation of `/play` plus `Content-Location`.

## Plan
- [x] `bash` `make test-airplay` - rerun the complete host AirPlay suite.
- [x] `bash` `git diff --check` - verify patch hygiene without disturbing unrelated work.
- [x] `bash` strict Switch trace build - verify compilation with current local dependencies.
- [x] `edit` plan records - record validation results and next trace markers.

## Quality Checklist
- [x] Evidence-before-edit: validation-only step; source edits occur only if a scoped failure is reproduced.
- [x] Existing pattern / reuse checked: reuse current make targets and VS Code trace flags.
- [x] Contract understood: host success does not claim real-device video until `/play` and media delivery appear.
- [x] Risk reviewed: stale generated artifacts and selected FFmpeg capability mismatch.
- [x] Mitigation recorded: report exact build result and avoid committing artifacts.

## Validation Checklist
- [x] `make test-airplay` exits 0.
- [x] `git diff --check` exits 0.
- [x] Strict Switch build exits 0 or the exact external dependency blocker is recorded.

## Test Checklist
- [x] Next real-device trace contract records `/play`, `Content-Location`, bridge start, and player handoff independently.

## Implementation Notes
The complete suite passed after the final bounds check. `make full-trace-build BUILD_JOBS=4` selected profile 14 with all media/input/AirPlay traces enabled and produced `NX-Cast.nro`. The next device trace must first show `/play`; only after that should bridge or FFmpeg failures be investigated.

## Files Changed
- `NX-Cast.nro` (generated, ignored artifact)
- `plans/2026-08-14-youtube-hls-compat/plan.md`
- `plans/2026-08-14-youtube-hls-compat/steps/step-2.md`
