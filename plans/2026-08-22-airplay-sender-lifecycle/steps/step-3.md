# Step 3: Enforce Current-Media Render Epoch

> Status: COMPLETED
> Created: 2026-08-22

## Goal
Prevent old render surfaces from being presented or reported as the replacement media's first frame, then validate all playback paths.

## Prerequisites
- Step 2 completed with stable generation replacement.
- Files to modify: player backend/session render readiness, frontend trace/presentation gate, focused tests, and AirPlay docs.
- Design: keep the existing deko3d render context and gate presentation by current media readiness.

## Deliverables
- Loading/replacement displays the loading renderer until the current media has restarted.
- First-frame trace belongs to the current media sequence.
- After this step: host suites and Switch full-trace build pass.

## Plan
- [x] `edit` player backend/session contract — expose current-media render readiness across `loadfile replace` and playback restart.
- [x] `edit` `source/player/backend/libmpv.c`, player API, and `source/player/render/frontend.c` — gate video render and first-frame trace by current-media readiness.
- [x] `edit` AirPlay documentation — record sender aggregation, detached grace, explicit-stop, and render epoch invariants.
- [x] `bash` `make test-airplay && make test-protocol-coordinator && make test-player-cache-policy` — zero failures.
- [x] `bash` `make full-trace-build BUILD_JOBS=4 && git diff --check` — successful NRO and clean whitespace.

## Quality Checklist
- [x] Evidence-before-edit: read render call path and backend state; searched all backend render APIs
- [x] Existing pattern / reuse checked: reuse startup gate and backend state, do not recreate the GPU backend
- [x] Contract understood: successful render is not current-media readiness until playback restart
- [x] Risk reviewed: black screen, first-frame regression, DLNA/IPTV startup delay
- [x] Mitigation recorded: loading renderer fallback and aggregate regression suite

## Validation Checklist
- [x] `make full-trace-build BUILD_JOBS=4` exits 0
- [x] `git diff --check` exits 0

## Test Checklist
- [x] `make test-airplay && make test-protocol-coordinator && make test-player-cache-policy` — all pass

## Implementation Notes
The backend now reports readiness directly from its current file state. The frontend keeps presenting the loading layer until libmpv has loaded and released the startup gate, so only a current-generation render can emit first-frame diagnostics.

## Files Changed
`source/player/backend.h`, `source/player/backend/libmpv.c`, `source/player/backend/mock.c`, `source/player/core/session.c`, `source/player/player.h`, `source/player/renderer.h`, `source/player/render/frontend.c`, `docs/AIRPLAY_DEVELOPMENT.md`, `docs/AIRPLAY_PROTOCOL_COMPATIBILITY.md`.
