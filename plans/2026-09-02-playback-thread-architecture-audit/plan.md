# Plan: Playback Thread Architecture Audit

> Status: COMPLETED
> Created: 2026-09-02
> Last Updated: 2026-09-02

## Goal
Determine whether current playback failures originate in protocol behavior or in socket, thread, and coordinator management by comparing the current implementation with the v0.2.0 baseline and latest runtime evidence.

## Assumptions
- `v0.2.0` is the useful pre-AirPlay release baseline where DLNA and IPTV playback were working.
- `logs/run_nxlink-20260902-012957.log` is the latest complete mixed-protocol trace available locally.

## Open Questions
None.

## Spec-Lite
### Acceptance Criteria
- [x] Current and historical playback thread/socket ownership are documented per protocol.
- [x] Each reported symptom is classified as protocol, transport/threading, coordinator, demux/decode, or source supply using concrete evidence.
- [x] Remaining unknowns and the minimum next trace required are explicit.

### Non-goals
- Editing playback, protocol, network, or state-machine code.
- Sender-specific compatibility patches.

### Edge Cases
- AirPlay control-only/audio-only attempts, reverse-HLS local HTTP workers, direct URL playback, and live IPTV starvation must not be conflated.

## Design Decisions
None — no design-sensitive changes.

## Steps Overview
| Step | File | Status | Goal |
|------|------|--------|------|
| Step 1 | `steps/step-1.md` | COMPLETED | Compare historical/current thread ownership and classify the latest failures. |

## Validation Commands
| Purpose | Command | Source | Required? |
|---|---|---|---|
| Current call graph | `rg -n "threadCreate|socket|accept|protocol_coordinator|PlayerSetMedia" source` | repository | yes |
| Historical comparison | `git show v0.2.0:<path>` and `git diff v0.2.0 -- <paths>` | Git | yes |
| Runtime classification | bounded `rg`/`sed` over latest log | device trace | yes |

## Context & Learnings
### Key Decisions
- Diagnose by resource lifetime: discovery, control connection, media supply, player actor, decoder, and UI are separate layers.
### Gotchas & Warnings
- Shutdown-time `Connection reset by peer` is not a playback failure.
- A sender that never sends `/play` is not evidence that the player thread stalled.

### Working Set
| Path | Role in this task | Evidence |
|------|-------------------|----------|
| `source/protocol/airplay/server.c` | AirPlay listener and connection workers | current source search |
| `source/protocol/dlna/http_server.c` | DLNA HTTP listener/workers | current source search |
| `source/player/core/media_actor.c` | serialized player command execution | current source search |
| `source/app/protocol_coordinator.c` | protocol ownership state machine | current source search |
| `logs/run_nxlink-20260902-012957.log` | latest runtime behavior | local log inventory |

### Verified Facts
- Current HEAD is on `airplay`; tags include `v0.2.0`, `v0.1.1`, and `v0.1.0` — verified by Git, 2026-09-02.
- `v0.2.0` used one serialized DLNA HTTP listener, one IPTV fetch worker, and one player event-pump thread; protocol/UI callers still entered backend commands directly under player synchronization.
- Current playback commands are serialized by one media actor while DLNA, SSDP, mDNS, AirPlay control, and IPTV workers keep their own network threads.
- AirPlay has four bounded control slots and four bounded loopback media slots; completed client workers are reaped and joined.
- The default build keeps receivers running during media playback, so the coordinator does not stop or restart protocol services for ordinary owner changes.
- The latest trace reports zero instrumented socket pressure/slot overflow, zero actor rejection/timeout, and zero failed coordinator transition while all discovery/control heartbeats continue.
- The latest YouTube failure reached `/play` and the player, then failed in HLS transport/track handling; failed Bilibili attempts ended during AirPlay negotiation before video intent; IPTV showed expired live-playlist segments while the network/control layers remained healthy.

## Implementation Log
| Date | Step | Summary |
|------|------|---------|
| 2026-09-02 | Step 1 | Compared `v0.2.0` and current thread ownership and classified the latest failures. No production code changed. |
