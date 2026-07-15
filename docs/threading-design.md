# Threading Design

This document describes the current concurrency model in `NX-Cast`.

## Goals

The threading model must ensure:

1. Protocol entry points do not block directly on heavy playback work.
2. Playback state has one real writer.
3. Protocol, playback, and eventing boundaries stay clear.

## Current Execution Units

Stable execution units:

1. `main` / foreground render loop.
2. `SSDP` thread.
3. `HTTP` thread.
4. Player owner thread.
5. `libmpv` event pumping through the player backend.
6. GENA event worker.

## Ownership Model

```text
HTTP/SOAP thread
  -> protocol handler
  -> renderer/player command
  -> player owner thread
  -> backend/libmpv
  -> PlayerEvent
  -> protocol_state
  -> event worker
```

The player owner thread is the only executor for playback control.

It is responsible for:

1. Opening media.
2. Playback commands.
3. Backend event processing.
4. Snapshot updates.
5. Emitting `PlayerEvent`.

This prevents SOAP threads from directly driving the backend and makes command ordering easier to reason about.

## protocol_state

`protocol_state` is not a thread. It is the shared protocol-facing state surface.

It is updated from player events and read by:

1. SOAP query actions.
2. `LastChange` generation.
3. Compatibility helpers.

This avoids duplicated runtime state across query and event paths.

## Render Boundary

Render-related ownership:

1. The main thread owns foreground pages and render-context lifecycle.
2. The backend does not own foreground UI lifecycle.
3. The backend exposes narrow render attachment and frame rendering hooks.
4. UI code must not call blocking playback operations from the render path.

## Rules

1. Prefer clear ownership over adding more threads.
2. Keep one real writer for playback state.
3. Do not add concurrency for hypothetical future needs.
4. Keep network/protocol work separate from render-context ownership.
5. Shut down in dependency order: UI/player first, protocol/server next, network and platform services last.

## Current Direction

Threading work should focus on:

1. Better shutdown sequencing.
2. Better logging around cross-thread commands.
3. Avoiding accidental render-thread blocking from touch or controller input.
4. Keeping protocol notifications asynchronous.
