# Step 1: Synchronize UI Frame Resources

> Status: COMPLETED
> Created: 2026-10-03

## Goal
Ensure GPU-owned UI resources are reused only after their frame fence signals, and clear standalone UI targets.

## Prerequisites
- Render path and deko3d fence APIs inspected.
- User confirmed per-slot resource design.

## Deliverables
- Per-slot command buffers and completion fences across home, loading, and overlay paths.
- Explicit clear for home/loading frames and safe cleanup after GPU completion.
- Successful Switch build and existing host UI tests, or documented external blocker.

## Plan
- [x] Edit `internal.h` and `frontend.c` to own per-slot command resources and fences.
- [x] Edit `imgui_overlay.cpp` and `frontend_overlay.c` to use the acquired slot and clear standalone frames.
- [x] Validate diff, build the Switch target, and run host UI tests.

## Quality Checklist
- [x] Evidence-before-edit: four render files read; all command buffer callers searched; Makefile validation identified.
- [x] Existing pattern / reuse checked: reuse existing two-slot swapchain and deko3d fences.
- [x] Contract understood: queue submissions are asynchronous and backing memory must outlive GPU use.
- [x] Risk reviewed: startup, loading/video transition, upload, fallback overlay, partial init, shutdown.
- [x] Mitigation recorded: slot fence wait before write, queue idle before destruction, no clear on video overlay.

## Validation Checklist
- [x] `git diff --check` exits 0.
- [x] Switch build succeeds with deko3d/ImGui release-required flags and with deko3d non-ImGui fallback flags.

## Test Checklist
- [x] `make test-ui` passes.
- [x] Real-device reproduction: unavailable locally; user follow-up remains necessary.

## Implementation Notes
Two overlay command buffers and their memory blocks now correspond to two swapchain slots. Each submitted slot receives a queue fence; its memory and ImGui vertex/index buffers are not reused until that fence signals. Standalone home/loading commands clear the target before drawing, while video overlays do not. Init failure and shutdown release resources after queue completion. Both Switch build configurations and host UI tests passed; there is no real-device result yet.

## Files Changed
- `source/player/render/internal.h`
- `source/player/render/frontend.c`
- `source/player/render/frontend_overlay.c`
- `source/player/render/imgui/imgui_overlay.cpp`
