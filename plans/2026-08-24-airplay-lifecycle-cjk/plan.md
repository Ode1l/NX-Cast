# Plan: AirPlay Lifecycle Boundaries and Chinese Text

> Status: COMPLETED
> Created: 2026-08-24
> Last Updated: 2026-08-24

## Goal
Align AirPlay connection and media lifetimes with protocol identities instead of peer-IP/timer heuristics, and render Chinese text reliably in the player UI and subtitles.

## Assumptions
- The packaged Source Han Sans CN font remains part of release and SD-card packaging.
- Chinese support covers channel names, programme metadata, media titles, UI status text, and text subtitles.

## Open Questions
None.

## Spec-Lite

### Acceptance Criteria
- [x] RAOP connections are not merged with HTTP `/play` or reverse connections solely because they share a peer IP.
- [x] HTTP `/play` and reverse connections can share a logical identity only through `X-Apple-Session-ID`.
- [x] Ordinary TCP close does not stop active URL playback after an arbitrary timeout; explicit `/stop`, stream-specific `TEARDOWN`, replacement, EOF, or error remains authoritative.
- [x] Common Simplified Chinese UI text renders from the packaged font and UTF-8 strings are never cut inside a multibyte code point.
- [x] libmpv is configured to discover the packaged Chinese font for text subtitles without relying on unavailable system font providers.

### Non-goals
- Full AirPlay 2 multi-room behavior, sender-specific website workarounds, Traditional Chinese full-glyph atlas, and dynamic runtime font-atlas rebuilding.

### Edge Cases
- Multiple devices behind one NAT address must remain separate; a reverse connection reconnect must not stop active media; missing packaged font must preserve the existing Switch-font fallback.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| AirPlay logical identity | Peer IPv4, fixed timeout, Apple session header plus stream identity | Apple session header for HTTP/reverse; RAOP and typed streams remain independent | yes, user challenged peer aggregation and grace behavior |
| Ordinary disconnect behavior | Stop immediately, stop after 5 seconds, transport-only detach | Transport-only detach; explicit protocol/media events own playback stop | yes, user requested protocol-correct behavior rather than patches |
| Chinese ImGui glyph set | Default Latin, 21k full CJK, ImGui common Simplified Chinese | Common Simplified Chinese range to bound Switch GPU memory | yes, requirement is Chinese support and bounded-memory platform behavior |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Replace peer/timer lifecycle heuristics with protocol-scoped identity and stop events. |
| Step 2 | `steps/step-2.md` | COMPLETED | Add bounded Chinese UI and subtitle font support with UTF-8-safe text handling. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| AirPlay unit/integration suite | `make test-airplay` | `makefile` target discovery | yes |
| Focused remote video tests | Host compile/run command recorded in Step 1 | `makefile` compile recipe | yes |
| C safety tests | `make test-c-safety` | `makefile` target discovery | yes |
| Switch build | `make dev-build` | repository ImGui/deko3d build target | yes |
| Patch hygiene | `git diff --check` | repository workflow | yes |

## Context & Learnings
### Key Decisions
- Protocol identity replaces topology identity: a sender IP is routing metadata, not an AirPlay session key.
- Media lifetime is not inferred from ordinary TCP lifetime because AirPlay control and reverse transports may reconnect independently.
- ImGui's common Simplified Chinese range is preferred over the 21k full range to avoid an oversized deko3d font texture.

### Gotchas & Warnings
- The worktree contains substantial existing AirPlay changes; edits must preserve unrelated user work.
- Existing `snprintf("%.Ns")` formatting can split UTF-8 sequences even after the correct glyph atlas is loaded.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/protocol/airplay/protocol/logical_session.[ch]` | Logical connection/session identity | `rg` and targeted reads found peer-IP association and teardown flags. |
| `source/protocol/airplay/receiver.c` | Connection classification and close routing | targeted read found peer observation and close-triggered media callbacks. |
| `source/protocol/airplay/media/remote_video.[ch]` | URL media ownership/lifetime | targeted read found 5-second detached timer and poll API. |
| `../others/UxPlay-master/lib/raop.c` | Reference receiver lifecycle | source comparison found per-connection destruction and global URL-video state without peer-IP session merging. |
| `../others/UxPlay-master/lib/raop_handlers.h` | Reference TEARDOWN semantics | source comparison found stream-type-specific audio/mirror teardown. |
| `../others/UxPlay-master/lib/http_handlers.h` | Reference URL playback lifecycle | source comparison found explicit `/stop`, playlist removal, and Apple session validation. |
| `source/player/render/imgui/imgui_overlay.cpp` | Player/home UI font and text rendering | `rg` found default-only glyph loading and byte-limited user text. |
| `source/player/backend/libmpv.c` | Subtitle/OSD font configuration | targeted read found `sub-font-provider=none`. |
| `assets/fonts/switch_font.ttf` | Packaged Chinese font | font metadata identifies Source Han Sans CN Regular. |
| `source/player/ui/utf8.[ch]` | Shared bounded UTF-8 text copy | focused host tests cover Chinese boundaries, output capacity, invalid input, and null input. |
| `scripts/test_player_utf8.c` | UTF-8 regression test | `make test-player-utf8` and `make test-c-safety` pass. |

### Verified Facts
- UxPlay shares one receiver object but identifies URL/reverse playback with Apple session identifiers and handles RTSP TEARDOWN by stream type; no peer-IP logical-session rule was found in the reference source — verified by local source inspection, 2026-08-24.
- The pre-change NX-Cast baseline bound RAOP to an HTTP logical session by peer IPv4 and stopped detached active playback through a 5000 ms poll deadline — verified by initial inspection of `logical_session.c`, `receiver.c`, and `remote_video.c`, 2026-08-24.
- The bundled ImGui offers `GetGlyphRangesChineseSimplifiedCommon()` and `GetGlyphRangesChineseFull()`; the pre-change NX-Cast loader selected only `GetGlyphRangesDefault()` — verified by initial inspection of `imgui.h`, `imgui_draw.cpp`, and `imgui_overlay.cpp`, 2026-08-24.
- The release font is Source Han Sans CN, but multiple IPTV/user-facing strings are truncated with byte-precision `snprintf`, which can generate invalid UTF-8 — verified by font metadata and `rg` over `imgui_overlay.cpp`, 2026-08-24.
- The packaged-font loader was compiled out because `NXCAST_USE_PACKAGED_FONT` was never defined; ImGui now always attempts the packaged font and falls back to Switch standard/Simplified Chinese/Nintendo fonts — verified by source search and successful `make dev-build`, 2026-08-24.
- The common Simplified Chinese atlas generated from the packaged font is 1024x2048 R8, approximately 2 MiB, with oversampling 1 — verified by a host-side ImGui atlas build, 2026-08-24.
- mpv documents that provider `none` still loads fonts from `sub-fonts-dir`; NX-Cast now points subtitle and OSD font directories to the packaged SD path and selects Source Han Sans CN — verified against the official mpv manual and successful Switch build, 2026-08-24.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-08-24 | Step 1 | Removed peer-IP RAOP aggregation and disconnect grace polling; ordinary active close now preserves playback, explicit `/play` replaces it, and full AirPlay tests pass. |
| 2026-08-24 | Step 2 | Enabled packaged/common Chinese glyphs, Switch Chinese fallback fonts, UTF-8-safe metadata clipping, and packaged libmpv subtitle fonts; safety tests, full AirPlay tests, and Switch build pass. |
