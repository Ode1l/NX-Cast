# Step 2: Mirror control capabilities

> Status: COMPLETED
> Created: 2026-10-03

## Goal
Hide mirror timeline and block seek without affecting URL video or DLNA.
## Prerequisites
- Step 1 completed; UI and backend seek callers inspected.
## Deliverables
- Consistent mirror capability in snapshots/UI, focused tests and Full Trace NRO.
## Plan
- [x] Edit UI/backend capability handling and renderer timeline conditions.
- [x] Add mirror-vs-URL playback regression.
- [x] Run UI tests and build using staged revision 4 FFmpeg.
## Quality Checklist
- [x] Read target and search touch/shoulder/backend seek callers; main.c and controls.c already gate on snapshot seekable.
- [x] Reuse existing seekable field and media URI identity; canonical URI shared via types.h.
- [x] Preserve normal video duration and controls; URL playback UI regression passes.
## Validation Checklist
- [x] Device build succeeds; link map points to staged revision 4 libavformat/libavcodec.
- [x] git diff --check succeeds.
## Test Checklist
- [x] make test-ui passes.
## Implementation Notes
Mirror snapshots from libmpv report non-seekable even if demuxer cache says otherwise, and backend rejects both immediate and deferred mirror seek. Both ImGui and fallback draw no mirror timeline. Tests use synthetic duration/seekable to ensure the UI still hides it. Full Trace NRO saved at artifacts/NX-Cast-mirror-late-audio-full-trace.nro; matching ELF at logs/NX-Cast-mirror-late-audio.elf, Build ID 0efec7439dff1bcf13e4e5f1c2c3437af09377de. Hardware retest remains user-run; global FFmpeg still old, so use upload-only until global dependency installed.
## Files Changed
source/player/types.h, types.c; source/player/backend/libmpv.c, libmpv_airplay.h; source/player/ui/bar.c, overlay.h; source/player/render/imgui/imgui_overlay.cpp, frontend_overlay.c; scripts/test_player_title.c, test_ui.sh.
