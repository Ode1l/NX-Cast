# Runtime Threading Design

This document is the executable concurrency contract for NX-Cast. The queue,
actor, supervisor, lifecycle handshake, health snapshots, and shutdown phases
described here are implemented and covered by host tests. Physical Switch
promotion remains a separate validation gate.

## Why This Exists

NX-Cast has several independent producers of media work:

1. Local controller and touch input.
2. IPTV catalog and channel selection.
3. DLNA HTTP/SOAP callbacks.
4. AirPlay control, mirror, timing, and audio callbacks.

These producers must not call the shared player directly. Protecting direct
calls with more mutexes still allows lock inversion, stale callbacks, blocking
startup, and protocol threads stopping media owned by another protocol.

The model follows the logging system's proven shape: producers enqueue
bounded messages and one worker owns the resource.

## Current Status

Implemented foundations:

1. A bounded Media Actor is the only executor of backend lifecycle, control,
   and event-pump operations.
2. UI, IPTV, DLNA, and AirPlay submit normalized source/generation commands;
   production call sites do not directly mutate the player.
3. IPTV, DLNA, and AirPlay have independent supervised startup workers, so one
   stalled service cannot block main input or another service.
4. Backend initialization is asynchronous. Main attaches the render context,
   then activates command dispatch, so media URLs cannot precede deko3d setup.
5. Actor, service, and logger health plus deterministic shutdown ordering are
   host-tested and visible in Full Trace heartbeats.
6. The logger starts before storage/network setup and keeps its bounded history
   in memory. Live nxlink is an optional nonblocking mirror; routine runtime
   logging never writes SD and health snapshots never acquire a sink I/O lock.
7. nxlink uses libnx's stderr duplication path from the v0.2.0 baseline, while
   the logger writes descriptor 2 with nonblocking `send()`. Stdout stays local,
   libmpv terminal output remains disabled, and a transient mirror failure is
   counted and retried instead of permanently disabling live trace.

The remaining release gate is the physical protocol-switching/soak matrix in
`plans/2026-07-21-runtime-concurrency-architecture/steps/step-6.md`.

## Non-Negotiable Invariants

1. The main thread owns input, ImGui/deko3d foreground state, render-context
   attachment/detachment, and frame presentation.
2. One Media Actor thread owns all player control commands, backend lifecycle,
   backend event pumping, and authoritative playback session state.
3. Protocol and UI code never calls libmpv or player backend control functions.
4. Protocol callbacks perform bounded parsing and command submission only.
5. No mutex is held while calling network, filesystem, FFmpeg, libmpv, a
   protocol callback, an event sink, or the logger.
6. Every media command identifies its producer and producer session. A stale
   session cannot mutate the currently active session.
7. Every queue is bounded. Queue-full behavior is explicit and observable.
8. Service startup and shutdown never run on the main/render thread.
9. Shutdown first prevents new producers, then destroys consumers and
   dependencies in order.
10. A visible home frame does not mean startup completed; main-loop heartbeat
    and input progress are separate health signals.

## Execution Units And Ownership

| Execution unit | Owns | May submit to | Must not do |
|---|---|---|---|
| Main/render thread | Input, home/video view, deko3d/ImGui, frame presentation | Media Actor, Protocol Supervisor, Logger | Network I/O, protocol start/stop, player control, waits for command completion |
| Media Actor | Player backend lifecycle, control commands, backend events, active media source/session, player snapshot | Logger, normalized player event sinks | Render-context/frame APIs, protocol parsing, blocking network/filesystem work under a lock |
| Protocol Supervisor | IPTV/DLNA/AirPlay lifecycle FSM and service snapshots | Service lifecycle operations, Logger | Player control, rendering, waiting on main |
| DLNA HTTP/SSDP/GENA workers | DLNA transport parsing and protocol responses | Media Actor, Logger | Direct player/backend calls, cross-protocol policy |
| IPTV worker | Catalog, playlist/EPG/logo network and filesystem work | IPTV state, Logger | Player/backend calls |
| AirPlay workers | RTSP, mDNS, pairing, timing, mirror/audio transport | Media Actor, Logger | Direct player/backend calls, render ownership |
| Logger worker | Runtime log history and the optional nxlink mirror | No runtime subsystem | Calling back into producers, holding a lock across sink I/O |

AirPlay's libmpv stream callback is a special data-plane boundary. It may read
from the cancellation-safe stream bridge because libmpv invokes it from its own
internal thread. It must not acquire coordinator, actor queue, or UI locks.

## Message Flow

```text
Controller / IPTV / DLNA / AirPlay
              |
              | submit MediaCommand (bounded, nonblocking)
              v
       +------------------+
       | Media Actor queue|
       +------------------+
              |
              | one executor
              v
       player backend / libmpv
              |
              | backend events
              v
       authoritative snapshot
              |
              +--> main reads immutable copy
              +--> DLNA protocol_state/eventing
              +--> runtime health/logging
```

The protocol response confirms command acceptance, not media load completion.
Actual progress is reported by player events and snapshots. This matches
libmpv's asynchronous command model and prevents protocol workers from waiting
for network probing or decoding.

## Media Command Contract

Each command envelope contains at least:

1. Monotonic command ID.
2. Command kind.
3. Producer (`APP`, `IPTV`, `DLNA`, `AIRPLAY_URL`, or `AIRPLAY_MIRROR`).
4. Producer session token.
5. Enqueue timestamp and optional execution deadline.
6. Deep-copied command payload.

Initial command kinds:

1. Open media, optionally with autoplay.
2. Play.
3. Pause.
4. Stop the matching session.
5. Stop any session for app shutdown/home policy.
6. Seek absolute or relative.
7. Set volume or mute.
8. Show OSD.
9. Quiesce and shutdown.

Before `Open`, the coordinator claims a lease and assigns a monotonically
increasing generation. The actor validates `(producer, session_token,
generation)` both before enqueue and immediately before execution. Every later
command must match that lease unless it is an app-level `StopAny` or shutdown
command.

Protocols create their session token before enqueueing `Open`:

1. DLNA increments its transport token on `SetAVTransportURI`.
2. IPTV increments its selection token when a channel/direct URL is chosen.
3. AirPlay uses its RTSP/mirror session identifier.
4. Main uses an application token for explicit home/shutdown commands.

This prevents an old callback from the same protocol from controlling a newer
session. The current ownership lease remains authoritative and is exposed in
supervisor snapshots and logs.

## Queue Policy

The initial media queue capacity is 64 commands with four slots reserved for
`StopAny`, `Quiesce`, and `Shutdown`.

Rules:

1. Enqueue performs only validation, payload copy, a short queue lock, and a
   condition-variable wakeup.
2. Normal commands are rejected with `BUSY` when normal capacity is exhausted.
3. Shutdown-class commands can use reserved slots and are never silently
   dropped.
4. Duplicate shutdown commands are idempotent.
5. Only replaceable absolute updates may be coalesced, such as setting volume
   to a newer absolute value for the same session. Relative seeks and volume
   deltas are never silently coalesced.
6. Queue depth, high-water mark, accepted, rejected, coalesced, expired, and
   stale counters are exposed in a health snapshot.
7. The actor processes a bounded command burst, then pumps backend events so a
   command storm cannot starve playback events.

Dynamic strings are owned by the command after successful enqueue and freed by
the actor after execution/rejection. Failed enqueue leaves ownership with the
caller. This rule must be represented by API names and tests, not comments only.

## Submission Semantics

Main/UI:

1. Always submit asynchronously.
2. Never wait for execution or media load.
3. Update visual state from player snapshots/events.

IPTV:

1. Channel selection submits one `Open(autoplay=true)` command.
2. A successful UI action means accepted into the actor queue.
3. Load failure is reflected later in player/IPTV status.

DLNA:

1. SOAP validates arguments and current protocol state.
2. It returns success when the normalized command is accepted.
3. Queue rejection maps to an explicit UPnP transition/action fault.
4. FIFO order preserves SetURI followed by Play, while session tokens reject
   commands from replaced transports.

AirPlay:

1. RTSP/control callbacks submit commands and return accepted/rejected.
2. Mirror stream data remains in the stream bridge data plane.
3. Teardown cancels the bridge first, then submits session Stop.

No production caller waits for probing, decoding, first frame, or backend event
completion.

## Media Actor Loop

The former player event worker is the Media Actor; a second player owner thread
is not used.

Conceptual loop:

```text
initialize backend asynchronously on the actor
publish CONTROL_READY
wait for main render attach and ACTIVATE
while not shutting down:
    wait for command or short event deadline
    pop at most N commands
    for each command:
        reject expired/stale command
        execute backend control without queue/snapshot locks
        publish command result and refreshed snapshot
    pump backend events
    publish heartbeat
stop current media
publish RENDER_DETACH_REQUIRED
wait for main-thread render detach acknowledgement
destroy backend
publish STOPPED
```

Backend initialization also belongs to the actor. Main observes readiness and
attaches the render context on its own thread. If initialization is slow, the
home UI remains interactive and displays player startup status.

## Render Boundary

The main thread exclusively calls:

1. View initialization and shutdown.
2. Render-context attachment and detachment.
3. `mpv_render_context_render` through the backend render facade.
4. ImGui/deko3d frame begin/end and presentation.

The Media Actor exclusively calls player control and event APIs. The render
facade may use a small backend pointer/state mutex, but it cannot share the
media command queue lock and cannot call protocol code.

Render attachment is a lifecycle handshake:

1. Actor publishes `CONTROL_READY`.
2. Main attaches render context and acknowledges `RENDER_READY`.
3. Supervisor may advertise media-capable protocols only after required player
   readiness is visible.
4. On shutdown, actor requests detach; main detaches; actor destroys backend.

## Protocol Supervisor FSM

The supervisor exposes immutable snapshots to main:

```text
STOPPED -> STARTING -> READY/DEGRADED -> STOPPING -> STOPPED
                    \-> FAILED (only when no usable service remains)
```

Each service independently transitions:

```text
DISABLED -> STARTING -> RUNNING
                    \-> FAILED
RUNNING/FAILED -> STOPPING -> STOPPED
```

Rules:

1. After the player render handshake, `main` requests startup and continues its
   input/render loop immediately.
2. Supervisor work runs outside the main thread and outside coordinator locks.
3. IPTV start allocates state and starts its worker; catalog/network work is a
   worker job, not synchronous startup work.
4. DLNA start creates local listener workers and returns; request handling is
   owned by those workers.
5. AirPlay retains its asynchronous receiver start but reports through the same
   service-state contract.
6. A failed service does not stop healthy services.
7. Startup age and last completed stage are observable. A watchdog reports a
   stall but never performs unsafe thread cancellation.

## Lock Rules

Allowed locks are narrow state-protection tools, not ownership mechanisms.

1. Queue lock: push/pop/counters only.
2. Player snapshot lock: copy/swap snapshot only.
3. Supervisor lock: copy/swap lifecycle state only.
4. Logger lock: queue, history, configuration flags, and counters only; sink
   configuration is complete before worker start and sink I/O is lock-free.
5. Stream bridge lock: bounded buffer/cancellation only.

Prohibited behavior while any of these locks is held:

1. Backend/libmpv/FFmpeg calls.
2. Socket or filesystem I/O.
3. Thread join or sleep.
4. Event sink or protocol callback invocation.
5. Logging.
6. Acquiring another subsystem lock.

This deliberately avoids defining a complex global lock order. Cross-subsystem
work moves through messages instead of nested locks.

## Health And Watchdog Data

Media Actor health:

1. Lifecycle state and heartbeat timestamp.
2. Queue depth/high-water mark and command counters.
3. Active command ID/kind/producer/session and command age.
4. Active media generation and player state.
5. Last backend event and timestamp.
6. Render attach/detach handshake state.

Protocol Supervisor health:

1. Overall and per-service state.
2. Transition age and last completed stage.
3. Last failure code/reason.
4. Stop requested/completed state.

Runtime heartbeats log identifiers and counters, not full signed URLs, pairing
keys, media payloads, or private protocol data.

## Shutdown Phases

Shutdown follows dependency direction:

1. `STOP_PRODUCERS`: close protocol listeners and cancel IPTV/AirPlay workers so
   no new commands can arrive.
2. `STOP_MEDIA`: cancel stream bridges and submit reserved `StopAny`.
3. `QUIESCE_AND_DRAIN`: reject new commands and wait until the actor has no
   queued or active command.
4. `DETACH_RENDER`: main thread releases ImGui/deko3d/libmpv render resources.
5. `DESTROY_PLAYER`: actor finalizes the backend on its own thread and exits.
6. `FLUSH_LOGGER`: drain remaining queued records into memory history/the
   optional nxlink mirror, then stop the logger.
7. `RELEASE_PLATFORM`: return through `userAppExit`, then release socket and
   platform resources in the platform exit hook.

Every phase is idempotent and logged before and after execution. A phase cannot
call a dependency that was already destroyed.

## Verification Strategy

Host tests must prove:

1. Single executor identity for every backend mutation.
2. FIFO command order and backend-event progress under command load.
3. Stale producer/session rejection after takeover.
4. Queue saturation and reserved shutdown behavior.
5. Main-facing submission/snapshot calls remain bounded while backend or
   service test doubles stall.
6. Stop during start/load and repeated shutdown are safe.
7. No command payload leaks or completion use-after-free under sanitizers.

Static source audit finds no production `renderer_*` or direct player mutation
calls outside player core/adapters. Physical promotion then
tests cold start, X input during startup, protocol switching, home during load,
reconnect, exit, and soak behavior with persistent SD logs.
