# Plan: Synchronize deko3d UI Frame Resources

> Status: COMPLETED
> Created: 2026-10-03
> Last Updated: 2026-10-03

## Goal
Prevent startup and UI rendering corruption by keeping per-frame GPU resources alive until the GPU finishes and explicitly clearing standalone UI frames.

## Assumptions
- Real-device reproduction is unavailable here; a successful build and source review do not prove the reported visual symptom is resolved.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] Two swapchain slots have independently reusable overlay command memory.
- [x] Slot vertex, index, and command memory is not overwritten or freed before a completion fence signals.
- [x] Home and loading frames clear the target before UI drawing; video overlays preserve the video frame.
- [x] Partial initialization and normal shutdown do not release in-flight GPU resources.

### Non-goals
- No new rendering thread, player state machine, or FFmpeg changes.
- No automatic overclock detection or frame retry heuristic.

### Edge Cases
- Slot acquire/render failure and fence timeout must not silently recycle GPU resources.
- Logo texture uploads and legacy C overlay must use safe command buffers.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| UI command lifetime | Global idle per frame; per-slot resources and fence | Per-slot resources and fence | Yes, user accepted preceding proposal |
| Standalone frame initialization | Rely on opaque ImGui rectangle; explicit target clear | Explicit target clear | Yes, user accepted preceding proposal |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Synchronize per-slot UI render resources and clear standalone frames, then build. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Diff check | `git diff --check` | Git | Yes |
| Switch build | `make NXCAST_USE_IMGUI_UI=1 NXCAST_REQUIRE_LIBMPV=1 NXCAST_REQUIRE_DEKO3D=1 -j4` | Makefile | Yes |
| Host UI tests | `make test-ui` | Makefile | Yes |
| Real device | Repeated cold starts and playback transitions | User hardware | Unavailable locally |

## Context & Learnings
### Key Decisions
- Reuse the existing two swapchain slots and renderer queue; do not introduce new scheduling layers.
### Gotchas & Warnings
- The same command buffer currently serves ImGui, logo upload, and legacy C overlay.
- This checkout contains unrelated dirty AirPlay and Docker changes, which must remain untouched.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/player/render/internal.h` | View-owned GPU resources | Read 2026-10-03 |
| `source/player/render/frontend.c` | Queue, swapchain, frame submission | Read 2026-10-03 |
| `source/player/render/frontend_overlay.c` | Legacy C overlay command submission | Read 2026-10-03 |
| `source/player/render/imgui/imgui_overlay.cpp` | ImGui buffers and draw commands | Read 2026-10-03 |

### Verified Facts
- Swapchain has two image slots; vertex and index buffers are already per-slot, but overlay command memory is shared — source reads and `rg`, 2026-10-03.
- `dkQueueSubmitCommands` uses externally managed memory, which must remain valid until GPU completion — devkitPro deko3d Primer, reviewed 2026-10-03.
- `make test-ui` exists and local cross-compiler is available — Makefile and command check, 2026-10-03.
- Both deko3d configurations (ImGui release-required and non-ImGui fallback) built successfully; host UI tests passed — local commands, 2026-10-03.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-10-03 | Step 1 | Added per-slot command buffers/fences and explicit standalone-frame clears; build and UI checks passed, real-device visual verification pending. |
