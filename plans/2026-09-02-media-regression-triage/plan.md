# Plan: Media Regression Triage

> Status: COMPLETED
> Created: 2026-09-02
> Last Updated: 2026-09-02

## Goal
Identify and repair the generic protocol, transport, or decoder regressions behind the latest AirPlay and IPTV failures without sender-specific behavior.

## Assumptions
- `logs/run_nxlink-20260902-010123.log` contains the YouTube, Bilibili, and IPTV run described by the user in chronological order.
- DLNA behavior must remain unchanged unless the trace proves a shared player defect.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [ ] The trace identifies separate initiating failures for YouTube, Bilibili, and IPTV rather than grouping downstream warnings together.
- [ ] Any code change follows generic AirPlay/media semantics and has focused regression coverage.
- [ ] AirPlay tests and the Switch build pass after the repair.
- [ ] Remaining device-only uncertainty is documented with exact trace evidence to collect.

### Non-goals
- Sender-, application-, host-, or advertisement-specific workarounds.
- Disabling hardware decoding as the product solution.

### Edge Cases
- Retry after a failed initial load, mid-stream URL replacement, fMP4 discontinuity/seek, audio-before-video startup, and transient IPTV buffering.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Repair boundary | Tune decoder blindly; treat SSL as root cause; reconstruct each session and patch the earliest receiver-controlled divergence | Evidence-first session reconstruction, then smallest generic fix | yes, follows the user's protocol-first requirement |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Reconstruct and classify the latest YouTube, Bilibili, and IPTV failures. |
| Step 2 | `steps/step-2.md` | COMPLETED | Implement and validate only evidence-backed generic repairs. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Focused tests | Relevant existing `scripts/test_airplay_*.c` or player test target selected after Step 1 | Makefile | yes |
| AirPlay suite | `make test-airplay` | Makefile | yes if AirPlay code changes |
| Switch build | `make dev-build BUILD_JOBS=4` | Makefile | yes |
| Hygiene | `git diff --check` | Git | yes |

## Context & Learnings
### Key Decisions
- A sender control close is treated as a consequence until an earlier receiver response or media failure is identified.
### Gotchas & Warnings
- Shutdown-time `Connection reset by peer` is not a playback root cause.
- TLS warnings must be tied to the requested media URL and timestamp before being treated as causal.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260902-010123.log` | Latest device trace | Newest file under `logs`; contains AirPlay, player, decoder, and protocol coordinator events. |
| `source/protocol/airplay/media/remote_hls.c` | AirPlay local HLS bridge | Candidate only if trace shows bridge segment or playlist failure. |
| `source/protocol/airplay/media/remote_video.c` | AirPlay URL control/status | Candidate only if trace shows control lifecycle or replacement failure. |
| `source/player/backend/libmpv.c` | Shared player configuration and lifecycle | Candidate for cache, demux, TLS, or hardware decode evidence. |

### Verified Facts
- The latest run reaches an AirPlay `/play`, local HLS `loadfile`, `file-loaded`, and repeated `/playback-info` polling, so initial discovery and control acceptance are not the first YouTube failure — verified by targeted log search, 2026-09-02.
- The trace includes MP4 `Packet corrupt`, invalid NAL size, and missing-picture messages around later media transitions; their causal session still needs bounded timeline correlation — verified by targeted log search, 2026-09-02.
- The build uses a 20 MiB forward, 10 MiB backward, 20-second network cache policy in this run — verified by `media-cache` trace, 2026-09-02.
- YouTube video corruption starts before the first TLS warning; the later `-0x7280` is mbedTLS `MBEDTLS_ERR_SSL_CONN_EOF`, not a certificate-validation error — verified by bounded trace correlation and the installed mbedTLS error definitions, 2026-09-02.
- AirPlay reverse-HLS retains three loopback HTTP connections while the control server, discovery services, and remote FFmpeg HTTPS connections share a 12-session BSD pool; local playlist responses currently do not request connection closure — verified by the trace and `airplay_remote_hls_serve`, 2026-09-02.
- On Bilibili direct playback, the last logical control connection closes before the first frame, but the protocol media session remains `control=attached`; known-good traces transitioned to detached without stopping media — verified against `run_nxlink-20260822-191736.log`, 2026-09-02.
- One IPTV URL is not recognized as media, while the CCTV HLS URL loads H.264/AAC successfully and then repeatedly underruns; these are separate source/transport outcomes rather than one IPTV parser failure — verified by sequence-bounded trace correlation, 2026-09-02.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-02 | Step 1 | Classified the AirPlay control-state regression, local HLS connection pressure, downstream decoder corruption, and separate IPTV source outcomes. |
| 2026-09-02 | Step 2 | Added explicit AirPlay control attach/detach synchronization and closed local playlist responses; AirPlay tests and Switch build passed. |
