# Plan: Diagnose AirPlay mirror startup stall

> Status: COMPLETE (device retest pending)
> Created: 2026-09-24
> Last Updated: 2026-09-24

## Goal
Handle a valid H.264 mirror codec configuration followed by extra bytes and make remaining first-packet failures diagnosable.

## Assumptions
- The latest trace in `logs/run_nxlink-20260924-155432.log` is the user's mirror test.
- The first codec packet may include fields after the parsed SPS/PPS; the log does not contain payload bytes, so this remains a hypothesis.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] A validated SPS/PPS configuration remains accepted when extra bytes follow it.
- [x] Truncated or invalid SPS/PPS are still rejected.
- [x] Rejected first codec packets log enough bounded metadata to identify the format without logging session secrets.
- [x] AirPlay host tests and Switch release build pass.
### Non-goals
- No change to pairing, session ownership, player or decoder behavior.
### Edge Cases
- Zero-size, short, oversized and malformed codec payloads remain invalid.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Codec suffix | Require exact end / ignore bounded suffix | Accept validated SPS/PPS and ignore suffix | Matches local UxPlay/RPiPlay behavior |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| 1 | `steps/step-1.md` | COMPLETE | Correct codec parsing and add focused failure diagnostics |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Host AirPlay tests | `make test-airplay` | makefile | yes |
| Release build | `make RELEASE_JOBS=4 release-build` | makefile | yes |
| Diff hygiene | `git diff --check` | Git | yes |

## Context & Learnings
### Key Decisions
- Only SPS/PPS bytes are needed by the H.264 bridge; no suffix is passed to the decoder.
### Gotchas & Warnings
- The user has an unrelated local README edit and plan; preserve both.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260924-155432.log` | Real-device symptom | Inspected setup through client disconnect |
| `source/protocol/airplay/mirror/video.c` | Codec config parser | Inspected `parse_parameter_sets` |
| `source/protocol/airplay/mirror/mirror_session.c` | Packet receive and close path | Inspected `process_client` |
| `scripts/test_airplay_mirror.c` | Host regression tests | Inspected existing config tests |
### Verified Facts
- First codec packet increments `config_failures` to 1, receives no video packet, and the mirror client disconnects, verified from the latest log.
- NX-Cast rejects any bytes after the last PPS, while local UxPlay and RPiPlay parse the SPS/PPS without exact-end enforcement, verified from source inspection.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-24 | 1 | Accepted valid avcC with trailing bytes, added bounded codec rejection logging and regression test. `make test-airplay`, release build, and diff check passed. Real-device result pending. |
