# Plan: AirPlay Playback Info Recovery

> Status: COMPLETED
> Created: 2026-09-02
> Last Updated: 2026-09-02

## Goal
Restore protocol-correct AirPlay remote-video control polling after first frame without sender-specific behavior or connection-lifetime workarounds.

## Assumptions
- `logs/run_nxlink-20260822-191736.log` is the known sustained-control baseline and `logs/run_nxlink-20260902-000335.log` is the current regression trace.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] `/playback-info` reports loaded ranges from the current playback position with a non-negative remaining duration.
- [x] Loading, playing, paused, and terminal snapshots produce internally consistent AirPlay fields.
- [x] Full AirPlay tests and the Switch build pass.
- [x] No sender name, URL host, peer-close timeout, or Bilibili-specific branch is introduced.

### Non-goals
- Forcing a sender TCP socket to remain open or synthesizing `/play` requests.

### Edge Cases
- Unknown duration, position beyond duration, loading before media metadata is available, paused playback, and completed media.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Control recovery | Reintroduce disconnect grace; keep current payload; align playback state with the receiver reference | Align generic `/playback-info` semantics with UxPlay and instrument the exact fields | yes, follows the user's protocol-first requirement |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Add protocol-state coverage and correct `/playback-info` range/status reporting. |
| Step 2 | `steps/step-2.md` | COMPLETED | Run full host/Switch validation and document the device trace contract. |

## Validation Commands

| Purpose | Command | Source | Required? |
|---|---|---|---|
| Focused test | Compile and run `scripts/test_airplay_remote_video.c` | Makefile `test-airplay` recipe | yes |
| Full suite | `make test-airplay` | Makefile | yes |
| Switch build | `make dev-build BUILD_JOBS=4` | Makefile | yes |
| Hygiene | `git diff --check` | Git | yes |

## Context & Learnings
### Key Decisions
- Treat sender disconnect as an observed consequence; repair only receiver-controlled protocol output.
### Gotchas & Warnings
- AirPlay URL playback control is HTTP/reverse HTTP, not DLNA SOAP.
- Do not interpret shutdown-time `Connection reset by peer` as a playback failure.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260822-191736.log` | Sustained-control baseline | Direct playback session 7 continues `/playback-info` polling for tens of seconds. |
| `logs/run_nxlink-20260902-000335.log` | Regression trace | Direct playback session 7 stops polling after request 14 near first frame. |
| `source/protocol/airplay/media/remote_video.c` | Builds remote playback status | Targeted read shows loaded range starts at zero and spans full duration. |
| `../others/UxPlay-master/lib/http_handlers.h` | Applicable receiver reference | Targeted read reports loaded range start at position and duration as duration minus position. |
| `scripts/test_airplay_remote_video.c` | Host protocol regression tests | Existing tests cover loading/active playback-info but not loaded-range arithmetic. |

### Verified Facts
- The sustained baseline and regression both accept direct `/play`, load the MP4, hardware-decode, and return HTTP 200 to `/playback-info`; they diverge in whether polling continues — verified by bounded log comparison, 2026-09-02.
- UxPlay reports `loadedTimeRanges[0]` as current position plus remaining duration, while NX-Cast reports zero plus full duration — verified by source comparison, 2026-09-02.
- The current disconnect occurs after successful protocol responses and first-frame presentation, so decoder failure is not the initiating event — verified from `run_nxlink-20260902-000335.log`, 2026-09-02.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-02 | Step 1 | Added failing position-relative range coverage, aligned loaded ranges with UxPlay semantics, and added redacted field-level Full Trace diagnostics. |
| 2026-09-02 | Step 2 | Passed the complete AirPlay host suite, Switch development build, and patch hygiene checks; retained real-device sender polling as the final runtime verification. |
