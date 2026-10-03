# Step 1: Correct codec configuration handling

> Status: COMPLETE (device retest pending)
> Created: 2026-09-24

## Goal
Accept validated H.264 codec parameters with a trailing extension and explain any remaining first-packet rejection in logs.

## Prerequisites
- Latest real-device trace and relevant local reference implementations inspected.
- Existing README edits must remain untouched.

## Deliverables
- Focused parser change and regression test.
- Bounded diagnostic line on invalid codec configuration.
- Passing host mirror test and release build.

## Plan
- [x] Update `video.c` to ignore a bounded suffix after validated SPS/PPS.
- [x] Add a test for valid configuration with suffix and retain malformed rejection tests.
- [x] Add short failure metadata in `mirror_session.c` for rejected first codec packets.
- [x] Run AirPlay tests, release build, and diff hygiene.

## Quality Checklist
- [x] Evidence-before-edit: target code and reference behavior inspected; host and Switch validation identified.
- [x] Existing pattern / reuse checked: existing parser and diagnostics macros reused.
- [x] Contract understood: validated SPS/PPS drive Annex B injection; suffix is not decoded.
- [x] Risk reviewed: accepting malformed config or leaking session secrets.
- [x] Mitigation recorded: preserve SPS/PPS checks; log only codec packet metadata and bounded bytes.

## Validation Checklist
- [x] `make test-airplay`
- [x] `make RELEASE_JOBS=4 release-build`
- [x] `git diff --check`

## Test Checklist
- [x] Valid config with suffix accepted; truncated config rejected; existing mirror tests pass.

## Implementation Notes
Device playback is not yet verified. If it still spins, inspect the new `codec-config-rejected` line from a Full Trace run before changing the parser again.

## Files Changed
- `source/protocol/airplay/mirror/video.c`
- `source/protocol/airplay/mirror/mirror_session.c`
- `scripts/test_airplay_mirror.c`
