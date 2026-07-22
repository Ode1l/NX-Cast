# Step 2: AirPlay Failure and Video Pipeline Diagnostics

> Status: COMPLETED
> Created: 2026-07-22

## Goal
Expose precise AirPlay setup failures, service-thread lifecycle, and aggregated mirror video pipeline progress without changing protocol behavior.

## Prerequisites
- Step 1 completed with runtime diagnostics API and Profile 14 gate available.
- Files to modify: AirPlay handlers, server/discovery/runtime/timing/audio/mirror/video/bridge sources and focused AirPlay tests.

## Deliverables
- Setup logs contain `ct/spf/sr` and failure stage labels including format, sockets, thread creation, bridge config, and response plist.
- Thread logs report created/joined/live/failed/generation for all requested AirPlay roles.
- Mirror logs aggregate accept/config/decrypt/AU/keyframe/bridge progress and failures.

## Plan
- [x] `read/rg` exact lifecycle transitions and all internal API callers.
- [x] `write/edit` focused tests that pin thread lifecycle and video-counter contracts.
- [x] `edit` AirPlay modules to record lifecycle and bounded diagnostic aggregates.
- [x] `bash` focused host tests where dependencies are available plus a complete Profile 14 Switch build.

## Quality Checklist
- [x] Evidence-before-edit: lifecycle and caller searches documented.
- [x] Existing pattern / reuse checked: used runtime/network registries and existing trace macros.
- [x] Contract understood: setup response codes, socket ownership, and thread joins remain unchanged.
- [x] Risk reviewed: concurrent clients, partial construction, counter races, per-packet overhead.
- [x] Mitigation recorded: atomic counters, explicit create-failed path, periodic/event aggregation only.

## Validation Checklist
- [x] AirPlay sources compile in available host tests and the complete Profile 14 Switch build.
- [x] Diagnostic logging is gated by Profile 14.

## Test Checklist
- [x] Server, mDNS, and timing focused host tests pass; handler/audio/mirror host execution is unavailable because this Cygwin environment lacks host mbedTLS/FFmpeg, while the same sources compile and link in the Switch build.
- [x] `make ... test-airplay-server-lifecycle` passes with lifecycle assertions.

## Implementation Notes
Added a Profile-14-only AirPlay diagnostics wrapper that logs created/joined/live/failed/generation and suppresses the old per-packet mirror trace in favor of one-second and connection-boundary aggregates. Instrumented mDNS, listener, client, timing, audio, mirror, and mirror-runtime threads plus timing/audio/mirror sockets. Audio SETUP now records `ct/spf/sr`; failures are labeled at format, data/control socket, thread-create, bridge-config, runtime-state, and response-plist boundaries. Video counters cover connection, encrypted/decrypted packets, AVCC config, AU drop/invalid/no-memory, keyframes, bytes, and bridge pushes. No return codes, player options, codec support, or ownership behavior changed. The full host suite was attempted but stopped in unrelated existing `source/player/types.c` because Cygwin exposes `strdup` without the required feature macro; this host also has no mbedTLS/FFmpeg development packages. Narrow host tests passed and the full Switch Profile 14 build linked successfully.

Global reflection explicitly initialized all dynamically allocated mirror-video atomic counters; the original `calloc` zero representation was not relied on as atomic initialization. It also removed the repeated-failure bypass from the bridge sampler so persistent push failures remain capped at one aggregate line per second.

## Files Changed
- `makefile`
- `scripts/test_airplay_mirror.c`
- `scripts/test_airplay_server_lifecycle.c`
- `scripts/test_airplay_stream_bridge.c`
- `source/protocol/airplay/diagnostics.h`
- `source/protocol/airplay/discovery/mdns.c`
- `source/protocol/airplay/media/mirror_runtime.c`
- `source/protocol/airplay/media/stream_bridge.c`
- `source/protocol/airplay/media/stream_bridge.h`
- `source/protocol/airplay/mirror/audio.c`
- `source/protocol/airplay/mirror/mirror_session.c`
- `source/protocol/airplay/mirror/mirror_session.h`
- `source/protocol/airplay/mirror/timing.c`
- `source/protocol/airplay/mirror/video.c`
- `source/protocol/airplay/mirror/video.h`
- `source/protocol/airplay/protocol/handlers.c`
- `source/protocol/airplay/server.c`
- `plans/2026-07-22-observability-profile-14/plan.md`
- `plans/2026-07-22-observability-profile-14/steps/step-2.md`
