# Player Layer

This document records the current `player` layer structure. It intentionally does not describe the removed pre-playback source modeling pipeline.

## Goals

The `player` layer solves four problems:

1. Receive normalized playback commands.
2. Maintain runtime playback state.
3. Drive a concrete playback backend.
4. Provide stable snapshots and events to the protocol layer.

It is not responsible for:

1. Protocol logic.
2. A separate source inference pipeline.
3. A second media opening strategy system inside `player`.

## Layering

```text
player
  -> core
  -> backend
  -> render
  -> ui
```

### Core

Responsibilities:

1. Own the Media Actor and bounded command queue.
2. Execute all backend control commands on the actor thread.
3. Pump backend events on the same actor thread.
4. Own active producer/session/generation state.
5. Cache and publish immutable `PlayerSnapshot` copies.
6. Forward normalized backend events without holding player locks.

Main files:

1. `source/player/core/session.c`
2. `source/player/core/media_actor.c`
3. `source/player/core/ownership.c`
4. `source/player/types.h`
5. `source/player/types.c`
6. `source/player/renderer.h`

### Backend

Responsibilities:

1. Execute real playback.
2. Integrate with `libmpv`.
3. Dispatch commands.
4. Observe properties and consume events.
5. Emit normalized `PlayerEvent` values.

Main files:

1. `source/player/backend/libmpv.c`
2. `source/player/backend/mock.c`

### Render

Responsibilities:

1. Switch the foreground video page.
2. Own render-context lifecycle.
3. Connect backend render callbacks through a narrow interface.
4. Return to the home page when no playback session is active.

Main files:

1. `source/player/render/view.c`
2. `source/player/render/frontend.c`
3. `source/player/render/frontend_overlay.c`

### UI

Responsibilities:

1. Keep playback overlay state.
2. Draw timeline and controls.
3. Map controller and touch input to player commands.

The release foreground UI is intentionally not a scrolling log console. The idle page is a static home/instruction screen, and it only surfaces the latest error while full log history stays available for debugging paths.

Main files:

1. `source/player/ui/overlay.c`
2. `source/player/ui/timeline.c`
3. `source/player/ui/controls.c`

## Public Interface

The player exposes three groups of data:

1. Commands.
2. `PlayerSnapshot`.
3. `PlayerEvent`.

### Commands

Command surface:

1. Open media with producer/session identity and optional autoplay.
2. Play or pause the matching session.
3. Stop the matching session or stop any session for app shutdown.
4. Seek absolute or relative.
5. Set volume or mute.
6. Show OSD.
7. Quiesce and shutdown the actor.

Submission returns accepted/rejected; it does not wait for probing, decoding,
or first frame. Main/UI never waits for command execution. Protocol handlers
map queue rejection to their own bounded error responses.

### PlayerSnapshot

`PlayerSnapshot` describes the current player state, not an event that just happened.

It currently includes:

1. `has_media`
2. `media`
3. `state`
4. `position_ms`
5. `duration_ms`
6. `volume`
7. `mute`
8. `seekable`

`PlayerMedia` uses dynamically owned `char *` fields for long strings.

### PlayerEvent

`PlayerEvent` describes a runtime change.

It currently carries:

1. `type`
2. `state`
3. `position_ms`
4. `duration_ms`
5. `volume`
6. `mute`
7. `seekable`
8. `error_code`
9. `uri`

## Playback Model

The current path is thin and asynchronous:

```text
protocol or UI action
  -> normalize + submit source-tagged MediaCommand
  -> bounded player queue
  -> Media Actor validates session/generation
  -> backend/libmpv asynchronous command
  -> libmpv runtime event
  -> PlayerEvent / PlayerSnapshot
  -> UI and protocol_state observe immutable state
```

Legacy synchronous wrappers are retained for tests and compatibility, but they
also submit through the Media Actor with bounded waits. Production UI and
protocol callers use the normalized command API directly.

`player_init()` starts backend initialization on the Media Actor and returns.
Main observes `backend_ready`, attaches the deko3d render context, then calls
`player_activate()`. Commands cannot execute before that handshake, preserving
the required render-context-before-URL order.

`SetAVTransportURI` sends the URL directly to the renderer. `libmpv/FFmpeg` is responsible for probing, networking, demuxing, decoding, and playback.

Startup policy, initial-Seek merging, direct-MP4 cache tuning, and stage timing are documented in [player-open-path.md](player-open-path.md).

## libmpv Integration

The backend has two lines of integration:

1. Command line: `loadfile`, `pause`, `seek`, `set_property`, and `stop`.
2. Observation line: `time-pos`, `duration`, `pause`, `mute`, `seekable`, `paused-for-cache`, `seeking`, EOF, and error events.

The protocol layer does not maintain a private playback model. It submits
commands downward and receives real runtime state back through player events
and snapshots. Protocol callbacks never call backend control APIs directly.

## Thread Ownership

Player control/event ownership and foreground rendering are intentionally
separate:

1. Media Actor: backend lifecycle, control commands, event pumping, snapshots.
2. Main thread: render-context attachment/detachment and frame presentation.
3. Protocol/UI producers: normalized command submission only.

No lock may be held across backend, network, filesystem, logger, event sink, or
protocol callback execution. See [threading-design.md](threading-design.md) for
the complete queue, lifecycle, overload, and shutdown contract.

## Render And Backend Routes

Current routes:

1. `libmpv`
2. `ao=hos`
3. `deko3d/libmpv render API`
4. `OpenGL/libmpv render API` fallback
5. `show-text` OSD bridge
6. `aud:a` process-volume first, `mpv volume` fallback

External-toolchain dependent parts:

1. Whether `hwdec=nvtegra` is actually available in the installed `FFmpeg/libmpv`.
2. Stability and compatibility of custom `FFmpeg/mpv` packages.

## Current Focus

Current player work should focus on:

1. Hardening `libmpv` event synchronization.
2. Keeping renderer state and protocol state aligned.
3. Hardening the local player UI and input control on the `deko3d` path.
4. Testing against real controllers and real URLs.
5. Keeping the OpenGL fallback and backend boundary clear.

Use `TRACE_MEDIA=1 TRACE_INPUT=1` when diagnosing player/UI problems, while normal builds keep the runtime log level suitable for release-style testing.
