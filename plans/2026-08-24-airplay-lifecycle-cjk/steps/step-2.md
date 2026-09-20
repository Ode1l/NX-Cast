# Step 2: Chinese Player Text and Subtitles

> Status: COMPLETED
> Created: 2026-08-24

## Goal
Render Chinese UI metadata and subtitles using the packaged font without invalid UTF-8 or unbounded font-atlas memory.

## Prerequisites
- Step 1 completed with AirPlay lifecycle tests passing.
- Files to modify: ImGui overlay font/text handling, libmpv font options, focused host tests or static checks, and packaging/user documentation if needed.
- Design: common Simplified Chinese atlas plus packaged subtitle font confirmed in the task overview.

## Deliverables
- Packaged ImGui font loads common Simplified Chinese glyphs.
- User-facing text truncation preserves UTF-8 boundaries.
- libmpv can resolve Source Han Sans CN for text subtitles from the SD-card font directory.
- After this step: C safety/AirPlay tests and Switch build pass.

## Plan
- [x] `rg`/`read` `source/player/render/imgui/imgui_overlay.cpp` — classified byte-truncated fields as user UTF-8 versus ASCII URLs/status internals.
- [x] `edit` `source/player/render/imgui/imgui_overlay.cpp` — load bounded Chinese glyph ranges and use UTF-8-safe clipping for names, groups, titles, and programme metadata.
- [x] `rg`/`edit` `source/player/backend/libmpv.c` — configure the packaged font directory/family for subtitles using documented mpv options.
- [x] `write` `source/player/ui/utf8.[ch]`, `scripts/test_player_utf8.c`, and `edit` `makefile` — add a reusable, host-tested UTF-8 boundary helper.
- [x] `bash` `make test-c-safety`, `make test-airplay`, `make dev-build`, host font-atlas check, and `git diff --check` — all required validation passed.

## Quality Checklist
- [x] Evidence-before-edit: font loader and all user-facing truncation sites read; mpv options verified against official documentation and the Switch build.
- [x] Existing pattern / reuse checked: ImGui built-in Chinese range and packaged Source Han font reused.
- [x] Contract understood: input strings are UTF-8; output must be valid, NUL-terminated UTF-8.
- [x] Risk reviewed: GPU atlas size, missing SD font, subtitle provider availability, and truncation correctness.
- [x] Mitigation recorded: bounded common glyph range, Switch Chinese fallback, 2 MiB atlas measurement, and focused UTF-8 tests.

## Validation Checklist
- [x] `make test-c-safety` exits 0.
- [x] `make test-airplay` exits 0.
- [x] `make dev-build` exits 0 with ImGui/deko3d/libmpv enabled.
- [x] `git diff --check` exits 0.

## Test Checklist
- [x] Chinese channel/programme/title samples remain valid UTF-8 after clipping.
- [x] Missing packaged font retains a Switch shared-font fallback including Simplified Chinese.
- [x] Packaged font and subtitle configuration paths match release SD-card layout.

## Implementation Notes
The ImGui loader now always attempts `sdmc:/switch/NX-Cast/fonts/switch_font.ttf`, uses the common Simplified Chinese glyph range with 1x oversampling, and falls back to Switch standard, Simplified Chinese, extended Simplified Chinese, and Nintendo extension fonts. A small C helper replaces byte-precision truncation for user-visible metadata and is covered by host tests. libmpv keeps system providers disabled but loads Source Han Sans CN through its documented font-directory options. The generated R8 atlas measured 1024x2048 (2 MiB).

## Files Changed
- `source/player/ui/utf8.h`
- `source/player/ui/utf8.c`
- `scripts/test_player_utf8.c`
- `source/player/render/imgui/imgui_overlay.cpp`
- `source/player/backend/libmpv.c`
- `makefile`
- `README.md`
- `docs/install.md`
