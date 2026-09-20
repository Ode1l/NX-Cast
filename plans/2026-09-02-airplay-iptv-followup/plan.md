# Plan: AirPlay and IPTV Follow-up

> Status: COMPLETE
> Created: 2026-09-02
> Last Updated: 2026-09-02

## Goal
Identify and repair the generic causes of the latest YouTube corruption/seek stall, Bilibili connection instability, and IPTV buffering without sender-specific behavior.

## Assumptions
- `logs/run_nxlink-20260902-012957.log` is the run just described by the user.
- The previous control detach and local playlist connection-close changes are present in the tested build.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [ ] Each reported symptom is mapped to its earliest trace event and active media session.
- [ ] The previous connection-close fix is verified on device rather than assumed effective.
- [ ] Any repair is protocol-, transport-, container-, or decoder-generic and has focused tests.
- [ ] AirPlay tests and the Switch build pass after any repair.

### Non-goals
- Sender-, advertisement-, host-, or application-specific workarounds.
- Software decoding as the product solution.

### Edge Cases
- Long seek across HLS discontinuities, reconnect after failed `/play`, direct MP4 control resynchronization, and low-throughput live IPTV.

## Design Decisions
| Decision | Options Considered | Chosen | Confirmed |
|----------|--------------------|--------|-----------|
| Investigation boundary | Tune codec/cache immediately; analyze latest run by session and fix the earliest receiver-controlled divergence | Evidence-first session reconstruction | yes, follows the user's protocol-first requirement |

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Reconstruct the latest YouTube, Bilibili, and IPTV timelines and classify initiating failures. |
| Step 2 | `steps/step-2.md` | COMPLETED | Implement and validate only verified generic repairs. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| AirPlay tests | `make test-airplay` | `makefile` | yes if AirPlay changes |
| Switch build | `make dev-build BUILD_JOBS=4` | `makefile` | yes |
| Hygiene | `git diff --check` | Git | yes |

## Context & Learnings
### Key Decisions
- Control transport, media ownership, network supply, demux, and decode are evaluated as separate lifetimes.
### Gotchas & Warnings
- Shutdown-time `Connection reset by peer` is not a playback root cause.
- A late TLS EOF cannot explain decode corruption that began earlier.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `logs/run_nxlink-20260902-012957.log` | Latest device trace | Newest log by modification time and matches the reported test window. |

### Verified Facts
- The latest log is `run_nxlink-20260902-012957.log`, 410975 bytes — verified by filesystem metadata, 2026-09-02.
- The prior local-playlist `Connection: close` repair is active: each local HLS response closes its worker socket and instrumented playback sockets fall to four after initialization.
- YouTube long seek first fails when FFmpeg attempts to reuse an HTTPS connection after the HLS segment host changes; repeated `Cannot reuse HTTP connection for different host` precedes the permanent seeking state.
- The YouTube master offers three H.264 variants and one VP9 variant, but mpv selects the VP9 variant marked default. The corrupt picture is therefore downstream of an avoidable codec selection mismatch.
- Bilibili's failed attempts complete pairing, SETUP, and RECORD with HTTP 200, then establish only RAOP audio and send TEARDOWN without `/play` or mirror video SETUP. Successful attempts send direct `/play` and reach first frame in 0.7-1.5 seconds.
- NX-Cast advertises `0x5A7FFEF7`, matching UxPlay when HLS support is enabled; changing feature bits is not supported by this trace.
- IPTV loads H.264/AAC successfully but repeatedly loses live segments, reports expired playlist skips and large PTS jumps, then underruns. Socket pressure is absent, so this one source does not justify a cache or state-machine change.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-02 | 1 | Isolated reverse-HLS cross-host keepalive and codec selection as receiver-controlled causes; Bilibili audio-only teardown and the tested IPTV source require no speculative receiver patch. |
| 2026-09-02 | 2 | Added a local reverse-HLS player profile with HTTP persistence disabled, filtered explicitly non-H.264 variants when H.264 is available, and added trace fields plus focused regression coverage. |
