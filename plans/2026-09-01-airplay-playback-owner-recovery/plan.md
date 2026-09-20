# Plan: AirPlay Playback and Owner Recovery

> Status: COMPLETED
> Created: 2026-09-01
> Last Updated: 2026-09-01

## Goal
Restore protocol-correct AirPlay HLS track selection and prevent IPTV controls from taking over AirPlay or DLNA playback.

## Assumptions
- The latest full trace is representative of the reported YouTube, Bilibili, and channel-menu behavior.
- Preserving all valid HLS variants is preferable to receiver-side bandwidth guessing because the media player owns codec/track selection.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] The local AirPlay master preserves and rewrites every valid media playlist reference from the sender master.
- [x] Large condensed playlists remain supported without fixed-line truncation.
- [x] IPTV channel controls and hints are available only while IPTV owns the active player lease.
- [x] Repeated AirPlay HLS/direct playback, coordinator, UI-adjacent host tests, and the Switch build pass.

### Non-goals
- Sender-specific YouTube/Bilibili branches, advertisement handling, codec forcing, or speculative lifecycle changes without a received second `/play`.

### Edge Cases
- Alternate audio groups, duplicate media URIs, more than one video variant, non-IPTV playback with an existing channel library, and a previously open IPTV drawer during protocol takeover.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| HLS variant ownership | Receiver selects one variant vs preserve sender master | Preserve all variants and let mpv choose, matching UxPlay flow | yes, follows user's protocol-first direction |
| Channel controls | Show whenever channels exist vs only IPTV owns playback | Gate render and input by active IPTV lease | yes, explicitly requested |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Restore complete HLS master/media relay with regression coverage. |
| Step 2 | `steps/step-2.md` | COMPLETED | Gate IPTV video controls and hints by active protocol ownership. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| AirPlay tests | `make test-airplay` | Makefile target | yes |
| IPTV navigation tests | `cc -std=c11 -Wall -Wextra -Werror -pedantic -Isource scripts/test_iptv_channel_list.c -o build/tests/test_iptv_channel_list && build/tests/test_iptv_channel_list` | Existing standalone host test | yes |
| Formatting | `git diff --check` | Git | yes |
| Switch build | `make dev-build BUILD_JOBS=4` | Makefile target | yes |

## Context & Learnings
### Key Decisions
- HLS parsing/relay remains in `remote_hls`; no codec policy is added to the protocol layer.
- The coordinator's active lease is the sole source of truth for protocol-specific player actions.
### Gotchas & Warnings
- A video owner takeover invalidates the previous lease immediately; exposing IPTV input during another protocol is a functional control-plane bug, not merely a misleading hint.
- Do not log signed playlist payloads or URLs beyond existing sanitized diagnostics.

> Append only. Never delete or rewrite existing entries below — only add new rows/facts as steps complete.
### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260901-223314.log` | Latest real-device trace | Ordered event search showed IPTV stealing AirPlay generation 2 and no second Bilibili AirPlay `/play`. |
| `source/protocol/airplay/media/remote_hls.c` | Master rewrite and FCUP media registry | Read found receiver-side highest-bandwidth pruning introduced in the previous task. |
| `../others/UxPlay-master/lib/http_handlers.h` | Applicable HLS relay reference | Read showed all master media URIs are requested before playback. |
| `source/main.c` | Video input dispatch and view-state construction | Read showed `X`, stick, and touch channel entry depend only on channel count. |
| `source/player/render/imgui/imgui_overlay.cpp` | Player action hints | Read showed `X Channels` depends only on channel count. |
| `scripts/test_airplay_remote_hls.c` | HLS relay regression tests | Read found a test enforcing the now-invalid single-variant policy. |

### Verified Facts
- YouTube reached HLS ready and mpv file-loaded before remaining in seeking; the selected media URL contained `itag/606`, then IPTV input took ownership and all AirPlay commands became stale — verified by ordered trace extraction, 2026-09-01.
- Bilibili's first AirPlay direct URL loaded in 271 ms and stopped cleanly; the next video arrived through DLNA and loaded in 151 ms, with no intervening second AirPlay `/play` — verified by trace extraction, 2026-09-01.
- UxPlay rewrites the full master and requests all listed media playlists; it does not choose the highest-bandwidth variant in the protocol layer — verified by local UxPlay source read, 2026-09-01.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-01 | Step 1 | Removed receiver-side highest-bandwidth pruning; all master variants/audio playlists are now locally mapped and covered by a four-media transcript test. |
| 2026-09-01 | Step 2 | Added a shared owner-aware IPTV menu rule, gated controller/touch/render paths, passed host suites and Switch build. |
