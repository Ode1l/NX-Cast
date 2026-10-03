# Step 1: Late audio negotiation

> Status: COMPLETED
> Created: 2026-10-03

## Goal
Accept late audio while keeping the existing video alive until a safe stream replacement.
## Prerequisites
- Runtime, bridge, video code and existing runtime tests read; UxPlay SETUP compared.
## Deliverables
- Transactional audio preparation, pending bridge lifecycle and keyframe replacement regression.
## Plan
- [x] Edit bridge audio preparation and runtime pending replacement using existing worker.
- [x] Include parameter sets on every keyframe.
- [x] Extend runtime tests for late audio, continuation and pending teardown.
- [x] Run protocol host tests.
## Quality Checklist
- [x] Target read and caller search completed; make test-airplay-session identified.
- [x] Existing REPLACE command reused; no added threads or protocol states.
- [x] Review ownership, queue capacity, stop races and keyframe gating; pending bridge ownership transfers under runtime mutex, freed on teardown.
## Validation Checklist
- [x] Protocol host test compilation succeeds.
- [x] Full protocol suite exits zero; final diff check follows UI step.
## Test Checklist
- [x] make test-airplay passes, including mirror runtime, bridge, audio and existing smoke suites.
## Implementation Notes
Audio waits for next keyframe rather than interrupting live video. Host tests exercise real encrypted mirror packets and late ct=8/spf=480 SETUP. No new threads or app-specific dispatch. Hardware audio latency while awaiting an IDR remains to be measured.
## Files Changed
source/protocol/airplay/media/stream_bridge.c, stream_bridge.h, mirror_runtime.c; source/protocol/airplay/mirror/video.c; scripts/test_airplay_mirror_runtime.c.
